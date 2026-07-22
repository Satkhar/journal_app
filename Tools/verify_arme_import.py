#!/usr/bin/env python3
"""Read-only cross-check of an Arme JSON v2 contract and imported SQLite DB."""

from __future__ import annotations

import argparse
import json
import sqlite3
from collections import Counter
from pathlib import Path


def scalar(connection: sqlite3.Connection, sql: str) -> int | str:
    row = connection.execute(sql).fetchone()
    if row is None:
        raise RuntimeError(f"Запрос не вернул строку: {sql}")
    return row[0]


def normalized_optional_date(value: object) -> str | None:
    return value if isinstance(value, str) else None


def expected_profile_rows(contract: dict[str, object]) -> set[tuple[object, ...]]:
    rows: set[tuple[object, ...]] = set()
    for profile in contract["participants"]:
        birthday = profile.get("birthday")
        training = profile.get("training_start_month")
        historical_name = profile["historical_name"]
        display_name = historical_name.strip() or profile["full_name"].strip()
        rows.add(
            (
                profile["id"],
                display_name,
                historical_name,
                profile["full_name"],
                profile["contact"],
                birthday.get("day") if birthday else None,
                birthday.get("month") if birthday else None,
                birthday.get("year") if birthday else None,
                profile["rank"],
                profile["combat_hand"],
                training.get("year") if training else None,
                training.get("month") if training else None,
                normalized_optional_date(profile.get("joined_club_on")),
                profile["notes"],
                None,
            )
        )
    return rows


def expected_row_sets(
    contract: dict[str, object],
) -> dict[str, set[tuple[object, ...]]]:
    history: set[tuple[object, ...]] = set()
    memberships: set[tuple[object, ...]] = set()
    days: set[tuple[object, ...]] = set()
    attendance: set[tuple[object, ...]] = set()
    markers: set[tuple[object, ...]] = set()
    for profile in contract["participants"]:
        for entry in profile["rank_history"]:
            history.add(
                (
                    profile["id"],
                    entry["rank"],
                    normalized_optional_date(entry.get("obtained_on")),
                )
            )
    for month in contract["months"]:
        year = month["year"]
        month_number = month["month"]
        checked = {
            (entry["participant_id"], entry["day"])
            for entry in month["attendance"]
        }
        for sort_order, participant_id in enumerate(month["participant_ids"]):
            memberships.add((year, month_number, participant_id, sort_order))
            for day in month["active_days"]:
                attendance.add(
                    (
                        year,
                        month_number,
                        day,
                        participant_id,
                        int((participant_id, day) in checked),
                    )
                )
        days.update((year, month_number, day) for day in month["active_days"])
        markers.update(
            (
                year,
                month_number,
                entry["day"],
                entry["participant_id"],
                entry["kind_mask"],
                entry["note"],
            )
            for entry in month["day_markers"]
        )
    return {
        "participants": expected_profile_rows(contract),
        "participant_rank_history": history,
        "month_participants": memberships,
        "month_days": days,
        "attendance": attendance,
        "participant_day_markers": markers,
    }


def database_row_sets(
    connection: sqlite3.Connection,
) -> dict[str, set[tuple[object, ...]]]:
    queries = {
        "participants": (
            "SELECT id, display_name, historical_name, full_name, contact, "
            "birth_day, birth_month, birth_year, rank, combat_hand, "
            "training_start_year, training_start_month, club_joined_on, "
            "notes, archived_at FROM participants"
        ),
        "participant_rank_history": (
            "SELECT participant_id, rank, obtained_on "
            "FROM participant_rank_history"
        ),
        "month_participants": (
            "SELECT year, month, participant_id, sort_order "
            "FROM month_participants"
        ),
        "month_days": "SELECT year, month, day FROM month_days",
        "attendance": (
            "SELECT year, month, day, participant_id, is_checked "
            "FROM attendance"
        ),
        "participant_day_markers": (
            "SELECT year, month, day, participant_id, kind_mask, note "
            "FROM participant_day_markers"
        ),
    }
    return {
        name: set(connection.execute(query).fetchall())
        for name, query in queries.items()
    }


def verify_exact_rows(
    expected: dict[str, set[tuple[object, ...]]],
    actual: dict[str, set[tuple[object, ...]]],
) -> None:
    for name, expected_rows in expected.items():
        actual_rows = actual[name]
        if actual_rows == expected_rows:
            continue
        missing = sorted(expected_rows - actual_rows, key=repr)[:3]
        unexpected = sorted(actual_rows - expected_rows, key=repr)[:3]
        raise RuntimeError(
            f"Таблица {name} не совпала с контрактом: "
            f"missing={missing}, unexpected={unexpected}"
        )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("contract", type=Path)
    parser.add_argument("database", type=Path)
    args = parser.parse_args()
    contract = json.loads(args.contract.read_text(encoding="utf-8"))

    expected_memberships = sum(
        len(month["participant_ids"]) for month in contract["months"]
    )
    expected_days = sum(
        len(month["active_days"]) for month in contract["months"]
    )
    expected_attendance_rows = sum(
        len(month["active_days"]) * len(month["participant_ids"])
        for month in contract["months"]
    )
    expected_checked = sum(
        len(month["attendance"]) for month in contract["months"]
    )
    expected_markers = sum(
        len(month["day_markers"]) for month in contract["months"]
    )
    expected_history = sum(
        len(profile["rank_history"]) for profile in contract["participants"]
    )

    connection = sqlite3.connect(f"file:{args.database}?mode=ro", uri=True)
    try:
        if scalar(connection, "PRAGMA integrity_check") != "ok":
            raise RuntimeError("PRAGMA integrity_check завершился ошибкой")
        foreign_key_errors = connection.execute(
            "PRAGMA foreign_key_check"
        ).fetchall()
        if foreign_key_errors:
            raise RuntimeError(f"Ошибки внешних ключей: {foreign_key_errors}")
        verify_exact_rows(
            expected_row_sets(contract), database_row_sets(connection)
        )
        checks = {
            "schema": (scalar(connection, "PRAGMA user_version"), 12),
            "participants": (
                scalar(connection, "SELECT COUNT(*) FROM participants"),
                len(contract["participants"]),
            ),
            "rank_history": (
                scalar(
                    connection,
                    "SELECT COUNT(*) FROM participant_rank_history",
                ),
                expected_history,
            ),
            "memberships": (
                scalar(connection, "SELECT COUNT(*) FROM month_participants"),
                expected_memberships,
            ),
            "active_days": (
                scalar(connection, "SELECT COUNT(*) FROM month_days"),
                expected_days,
            ),
            "attendance_rows": (
                scalar(connection, "SELECT COUNT(*) FROM attendance"),
                expected_attendance_rows,
            ),
            "checked_attendance": (
                scalar(
                    connection,
                    "SELECT COUNT(*) FROM attendance WHERE is_checked = 1",
                ),
                expected_checked,
            ),
            "markers": (
                scalar(
                    connection,
                    "SELECT COUNT(*) FROM participant_day_markers",
                ),
                expected_markers,
            ),
        }
        mismatches = {
            name: values for name, values in checks.items() if values[0] != values[1]
        }
        if mismatches:
            raise RuntimeError(f"Не совпали контрольные числа: {mismatches}")

        expected_months = {
            (month["year"], month["month"]): (
                len(month["participant_ids"]),
                len(month["attendance"]),
                len(month["day_markers"]),
            )
            for month in contract["months"]
        }
        actual_months = {}
        for year, month in connection.execute(
            "SELECT DISTINCT year, month FROM month_days ORDER BY year, month"
        ):
            membership_count = scalar(
                connection,
                "SELECT COUNT(*) FROM month_participants "
                f"WHERE year = {int(year)} AND month = {int(month)}",
            )
            checked_count = scalar(
                connection,
                "SELECT COUNT(*) FROM attendance "
                f"WHERE year = {int(year)} AND month = {int(month)} "
                "AND is_checked = 1",
            )
            marker_count = scalar(
                connection,
                "SELECT COUNT(*) FROM participant_day_markers "
                f"WHERE year = {int(year)} AND month = {int(month)}",
            )
            actual_months[(year, month)] = (
                membership_count,
                checked_count,
                marker_count,
            )
        if actual_months != expected_months:
            raise RuntimeError(
                f"Помесячные контрольные числа не совпали: {actual_months}"
            )

        marker_kinds: Counter[str] = Counter()
        for (mask,) in connection.execute(
            "SELECT kind_mask FROM participant_day_markers"
        ):
            for name, flag in (
                ("оплата", 0x01),
                ("железная тренировка", 0x02),
                ("первая тренировка", 0x04),
                ("другое", 0x08),
            ):
                if mask & flag:
                    marker_kinds[name] += 1

        print("SQLite integrity_check: ok")
        print("SQLite foreign_key_check: ok")
        for name, (actual, _) in checks.items():
            print(f"{name}: {actual}")
        print(
            "marker_kinds: "
            + ", ".join(
                f"{name}={count}" for name, count in marker_kinds.items()
            )
        )
        print("months:")
        for (year, month), values in actual_months.items():
            print(
                f"  {year:04d}-{month:02d}: participants={values[0]}, "
                f"attendance={values[1]}, markers={values[2]}"
            )
    finally:
        connection.close()


if __name__ == "__main__":
    main()
