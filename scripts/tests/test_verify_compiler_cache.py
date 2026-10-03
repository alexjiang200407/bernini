"""verify_compiler_cache: what counts as the same object, and which units it samples."""

import struct

import verify_compiler_cache as v


def coff(timestamp, body=b"\x01\x02\x03"):
    return struct.pack("<HHI", 0x8664, 3, timestamp) + body


def bigobj(timestamp, body=b"\x01\x02\x03"):
    return b"\x00\x00\xff\xff" + struct.pack("<HHI", 2, 0x8664, timestamp) + body


def test_two_objects_differing_only_in_timestamp_are_the_same_object():
    assert v.normalize_object(coff(1)) == v.normalize_object(coff(2))
    assert v.normalize_object(bigobj(1)) == v.normalize_object(bigobj(2))


def test_a_difference_in_the_object_body_is_never_normalised_away():
    assert v.normalize_object(coff(1, b"a")) != v.normalize_object(coff(1, b"b"))
    assert v.normalize_object(bigobj(1, b"a")) != v.normalize_object(bigobj(1, b"b"))


def test_sampling_is_repeatable_and_skips_what_is_not_an_object_compile():
    entries = [{"file": f"a{i}.cpp", "output": f"a{i}.obj"} for i in range(20)]
    entries += [{"file": "r.rc", "output": "r.res"}, {"file": "x.cpp"}]

    first = v.pick_sample(entries, 5, seed=3)

    assert first == v.pick_sample(list(reversed(entries)), 5, seed=3)
    assert len(first) == 5
    assert all(e["output"].endswith(".obj") for e in first)
