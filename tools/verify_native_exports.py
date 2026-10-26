"""Check the DLL exports required by Dusklight, directly from the packaged PE file."""
import struct
import sys
import zipfile

REQUIRED = {'mod_meta', 'mod_ctx', 'mod_initialize', 'mod_update', 'mod_shutdown'}
STREAM_LOOKUP = b'?getStreamFileEntry@Z2SoundInfo@@UEAAHVJAISoundID@@PEBUStreamReplacementSlot@bst@audio_res@svc@mods@dusk@@@Z'


def exports(data):
    def u16(offset):
        return struct.unpack_from('<H', data, offset)[0]

    def u32(offset):
        return struct.unpack_from('<I', data, offset)[0]

    assert data[:2] == b'MZ', 'Not a PE file'
    pe = u32(0x3C)
    assert data[pe:pe + 4] == b'PE\0\0', 'Invalid PE signature'
    optional = pe + 24
    magic = u16(optional)
    assert magic in (0x10B, 0x20B), 'Unsupported PE optional header'
    directory = optional + (112 if magic == 0x20B else 96)
    section_start = optional + u16(pe + 20)
    sections = []
    for i in range(u16(pe + 6)):
        section = section_start + i * 40
        sections.append((u32(section + 12), max(u32(section + 8), u32(section + 16)),
                         u32(section + 20)))

    def offset(rva):
        for base, size, raw in sections:
            if base <= rva < base + size:
                return raw + rva - base
        raise AssertionError(f'Unmapped RVA {rva:#x}')

    export_directory = offset(u32(directory))
    count = u32(export_directory + 24)
    names = offset(u32(export_directory + 32))
    result = set()
    for i in range(count):
        start = offset(u32(names + 4 * i))
        result.add(data[start:data.index(b'\0', start)].decode('ascii'))
    return result


if __name__ == '__main__':
    with zipfile.ZipFile(sys.argv[1]) as archive:
        assert archive.testzip() is None, 'Archive CRC failure'
        native = archive.read('lib/windows-amd64/mod.dll')
        names = exports(native)
    missing = REQUIRED - names
    assert not missing, f'Missing required mod API exports: {sorted(missing)}'
    print('PASS: all five required native exports are present: ' + ', '.join(sorted(REQUIRED)))
    if len(sys.argv) > 2:
        from pathlib import Path
        game_names = exports(Path(sys.argv[2]).read_bytes())
        assert STREAM_LOOKUP not in native, 'Obsolete native stream lookup hook still present'
        assert '?processBgmFramework@Z2SeqMgr@@QEAAXXZ' in game_names, 'Native audio framework capture hook missing'
        for name in ['?bgmStart@Z2SeqMgr@@QEAAXIIH@Z', '?bgmStop@Z2SeqMgr@@QEAAXIH@Z',
                     '?bgmStreamPrepare@Z2SeqMgr@@QEAAXI@Z', '?bgmStreamPlay@Z2SeqMgr@@QEAAXXZ',
                     '?bgmStreamStop@Z2SeqMgr@@QEAAXI@Z']:
            assert name in game_names, 'Native music suppression/restore API missing: '+name
        assert b'waveOutWrite' in native and b'waveOutRestart' in native, 'Windows PCM playback imports missing'
        print('PASS: Windows PCM imports and all native music suppression/restore hook signatures exist')
