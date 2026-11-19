#!/usr/bin/env python3
from __future__ import annotations

import argparse
import asyncio
import sys
from dataclasses import dataclass
from typing import Iterable, Optional

from bleak import BleakClient, BleakScanner

DEFAULT_NAME = "T5E1-EYES"
TUYA_SERVICE_UUID = "0000fd50-0000-1000-8000-00805f9b34fb"


@dataclass
class DeviceChoice:
    address: str
    name: str
    rssi: Optional[int]


@dataclass
class CharacteristicSet:
    service_uuid: str
    write_uuid: str
    write_with_response: bool
    notify_uuid: Optional[str]
    read_uuid: Optional[str]


def _normalize_uuid(value: str) -> str:
    return value.lower()


def _looks_like_tuya_service(uuid: str) -> bool:
    normalized = _normalize_uuid(uuid)
    return normalized == TUYA_SERVICE_UUID or normalized.endswith("fd50")


def _decode_payload(data: bytearray | bytes | memoryview) -> str:
    return bytes(data).decode("utf-8", errors="replace").strip("\x00\r\n ")


async def discover_devices(timeout: float) -> list[DeviceChoice]:
    found = await BleakScanner.discover(timeout=timeout, return_adv=True)
    devices: list[DeviceChoice] = []

    for _, pair in found.items():
        device, adv = pair
        name = device.name or adv.local_name or "Unknown"
        devices.append(
            DeviceChoice(
                address=device.address,
                name=name,
                rssi=adv.rssi,
            )
        )

    devices.sort(key=lambda item: (item.name != DEFAULT_NAME, item.name, item.address))
    return devices


async def find_target_device(name: str, address: Optional[str], timeout: float) -> DeviceChoice:
    devices = await discover_devices(timeout)

    if address:
        for device in devices:
            if device.address.lower() == address.lower():
                return device
        raise RuntimeError(f"Device with address {address} not found.")

    for device in devices:
        if device.name == name:
            return device

    visible = ", ".join(f"{device.name} [{device.address}]" for device in devices) or "none"
    raise RuntimeError(f"Device named {name!r} not found. Visible devices: {visible}")


def resolve_characteristics(client: BleakClient) -> CharacteristicSet:
    write_uuid: Optional[str] = None
    notify_uuid: Optional[str] = None
    read_uuid: Optional[str] = None
    write_with_response = True
    matched_service_uuid: Optional[str] = None

    for service in client.services:
        if not _looks_like_tuya_service(service.uuid):
            continue

        matched_service_uuid = service.uuid
        for char in service.characteristics:
            properties = {prop.lower() for prop in char.properties}
            if write_uuid is None and ("write" in properties or "write-without-response" in properties):
                write_uuid = char.uuid
                write_with_response = "write" in properties
            if notify_uuid is None and ("notify" in properties or "indicate" in properties):
                notify_uuid = char.uuid
            if read_uuid is None and "read" in properties:
                read_uuid = char.uuid

    if matched_service_uuid is None:
        raise RuntimeError("Could not find the Tuya common BLE service (0xFD50).")
    if write_uuid is None:
        raise RuntimeError("Could not find a writable command characteristic on the Tuya BLE service.")

    return CharacteristicSet(
        service_uuid=matched_service_uuid,
        write_uuid=write_uuid,
        write_with_response=write_with_response,
        notify_uuid=notify_uuid,
        read_uuid=read_uuid,
    )


async def cmd_scan(args: argparse.Namespace) -> int:
    devices = await discover_devices(args.timeout)
    if not devices:
        print("No BLE devices found.")
        return 1

    for device in devices:
        marker = "*" if device.name == DEFAULT_NAME else " "
        rssi = "?" if device.rssi is None else str(device.rssi)
        print(f"{marker} {device.name:<24} {device.address:<36} RSSI={rssi}")
    return 0


async def cmd_send(args: argparse.Namespace) -> int:
    target = await find_target_device(args.name, args.address, args.timeout)
    print(f"Connecting to {target.name} [{target.address}]")

    async with BleakClient(target.address, timeout=args.timeout) as client:
        if not client.is_connected:
            raise RuntimeError("BLE connection failed.")

        chars = resolve_characteristics(client)
        print(f"Service: {chars.service_uuid}")
        print(f"Write:   {chars.write_uuid}")
        if chars.notify_uuid:
            print(f"Notify:  {chars.notify_uuid}")
        if chars.read_uuid:
            print(f"Read:    {chars.read_uuid}")

        notifications: list[str] = []
        notification_event = asyncio.Event()

        def _notification_handler(_: str, data: bytearray) -> None:
            text = _decode_payload(data)
            notifications.append(text)
            print(f"notify> {text}")
            notification_event.set()

        if chars.notify_uuid:
            await client.start_notify(chars.notify_uuid, _notification_handler)
            await asyncio.sleep(0.2)

        async def _read_reply() -> None:
            if not chars.read_uuid:
                return
            payload = await client.read_gatt_char(chars.read_uuid)
            text = _decode_payload(payload)
            if text:
                print(f"read>   {text}")

        for command in args.commands:
            notification_event.clear()
            print(f"send>   {command}")
            await client.write_gatt_char(
                chars.write_uuid,
                command.encode("utf-8"),
                response=chars.write_with_response,
            )

            if chars.notify_uuid:
                try:
                    await asyncio.wait_for(notification_event.wait(), timeout=args.response_timeout)
                except asyncio.TimeoutError:
                    pass

            await asyncio.sleep(args.settle)
            await _read_reply()

        if chars.notify_uuid:
            await asyncio.sleep(args.listen)
            await client.stop_notify(chars.notify_uuid)

    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Mac-side BLE command agent for the T5E1 eye firmware."
    )
    sub = parser.add_subparsers(dest="command", required=True)

    scan = sub.add_parser("scan", help="Scan nearby BLE peripherals.")
    scan.add_argument("--timeout", type=float, default=5.0, help="Scan duration in seconds.")
    scan.set_defaults(func=cmd_scan)

    send = sub.add_parser("send", help="Send one or more commands to the eye agent.")
    send.add_argument("commands", nargs="+", help="Commands like 'STATUS' or 'SET HAPPY'.")
    send.add_argument("--name", default=DEFAULT_NAME, help="Advertised BLE name to target.")
    send.add_argument("--address", help="Specific BLE address/identifier to connect to.")
    send.add_argument("--timeout", type=float, default=10.0, help="Scan/connect timeout in seconds.")
    send.add_argument("--settle", type=float, default=0.25, help="Delay after each write before readback.")
    send.add_argument("--listen", type=float, default=0.5, help="Extra time to keep notifications open.")
    send.add_argument(
        "--response-timeout",
        type=float,
        default=1.0,
        help="How long to wait for a notification after each command.",
    )
    send.set_defaults(func=cmd_send)

    return parser


def main(argv: Optional[Iterable[str]] = None) -> int:
    parser = build_parser()
    args = parser.parse_args(list(argv) if argv is not None else None)

    try:
        return asyncio.run(args.func(args))
    except KeyboardInterrupt:
        print("Interrupted.", file=sys.stderr)
        return 130
    except Exception as exc:  # noqa: BLE001
        print(f"Error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
