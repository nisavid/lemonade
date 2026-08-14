"""Scan and slice ATX-heading Markdown sections outside fenced code blocks."""

from __future__ import annotations

import re
from dataclasses import dataclass


@dataclass(frozen=True)
class MarkdownHeading:
    """One ATX heading with offsets into its canonical document text."""

    start: int
    content_end: int
    level: int
    title: str


def scan_markdown_headings(document: str) -> list[MarkdownHeading]:
    """Return raw ATX headings while ignoring headings inside fenced blocks."""

    headings: list[MarkdownHeading] = []
    fence_character: str | None = None
    fence_length = 0
    offset = 0
    for line in document.splitlines(keepends=True):
        content = line.rstrip("\r\n")
        fence = re.match(r"^ {0,3}(`{3,}|~{3,})", content)
        if fence is not None:
            marker = fence.group(1)
            if fence_character is None:
                fence_character = marker[0]
                fence_length = len(marker)
            elif (
                marker[0] == fence_character
                and len(marker) >= fence_length
                and content[fence.end() :].strip() == ""
            ):
                fence_character = None
                fence_length = 0
            offset += len(line)
            continue
        if fence_character is None:
            heading = re.match(r"^(#{1,6})[ \t]+(.+?)[ \t]*$", content)
            if heading is not None:
                headings.append(
                    MarkdownHeading(
                        start=offset + heading.start(),
                        content_end=offset + heading.end(),
                        level=len(heading.group(1)),
                        title=heading.group(2),
                    )
                )
        offset += len(line)
    return headings


def slice_markdown_section(
    document: str,
    headings: list[MarkdownHeading],
    selected: MarkdownHeading,
    *,
    include_heading: bool,
) -> str:
    """Slice one heading section through the next peer-or-parent heading."""

    end = len(document)
    for heading in headings:
        if heading.start > selected.start and heading.level <= selected.level:
            end = heading.start
            break
    start = selected.start if include_heading else selected.content_end
    return document[start:end]
