from pathlib import Path
import hashlib,json,sys,zipfile
root=Path(__file__).resolve().parents[1]
manifest=json.loads((root/'assets.sha256.json').read_text())
if len(sys.argv)>1:
    with zipfile.ZipFile(sys.argv[1]) as archive:
        assert archive.testzip() is None
        for path,digest in manifest.items():
            assert hashlib.sha256(archive.read(path)).hexdigest()==digest,path
else:
    for path,digest in manifest.items():
        assert hashlib.sha256((root/path).read_bytes()).hexdigest()==digest,path
print('PASS: all approved logo, soundtrack and scene assets match the release manifest')
