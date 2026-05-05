from typing import List
from struct import pack


def serialize_float32(vector: List[float]) -> bytes:
    """Serializes a list of floats into the "raw bytes" format sqlite-vec expects"""
    return pack("%sf" % len(vector), *vector)


def serialize_int8(vector: List[int]) -> bytes:
    """Serializes a list of integers into the "raw bytes" format sqlite-vec expects"""
    return pack("%sb" % len(vector), *vector)


