"""
minis_mqtt_vfs.py — VFS extension for MinisIoT (MicroPython)

Exposes the device's internal filesystem over MQTT using the MyCastle VFS
protocol.  The server can browse, read, write, and manage files on the device
through the MyCastle VFS Explorer.

Protocol:
  Request  topic: minis/{user}/{device}/ext/vfs/req
  Response topic: minis/{user}/{device}/ext/vfs/res

Supported operations:
  stat      path → { type: 1|2, size, ctime, mtime }
  readdir   path → { entries: [{ name, type }] }
  readfile  path → { data: base64 }
  writefile path + data (base64) + options:{create?,overwrite?} → {}
  delete    path + options:{recursive?} → {}
  rename    path + newPath + options:{overwrite?} → {}
  mkdir     path → {}

FileType values (match packages/core/src/vfs/types.ts):
  1 = File, 2 = Directory

Usage::

    from minis_iot import MinisIoT
    from minis_mqtt_vfs import MinisMqttVfs

    minis = MinisIoT(host, port, user_id, device_id)
    vfs = MinisMqttVfs(minis, root='/')
    minis.begin()

    while True:
        minis.loop()
"""

import os
import ubinascii

# FileType constants — must match packages/core/src/vfs/types.ts FileType enum
_FILE = 1
_DIR  = 2

_STAT_MODE_DIR = 0x4000   # st_mode bit set for directories in MicroPython


class MinisMqttVfs:
    """
    MinisIoT VFS extension — exposes the internal MicroPython filesystem
    over MQTT so MyCastle can browse and manage files on the device.

    :param minis: MinisIoT instance to register with.
    :param root:  Root path exposed to the server (default ``'/'``).
                  Only paths under this root are accessible.
    """

    EXT_TYPE = 'vfs'

    def __init__(self, minis, root='/'):
        self._minis = minis
        # Normalise root: no trailing slash, keep bare '/'
        self._root = root.rstrip('/') if root != '/' else '/'
        minis.add_extension(self.EXT_TYPE, self._on_request)

    # ── Extension callback ─────────────────────────────────────────────────────

    def _on_request(self, req_id, op, params):
        """Called by MinisIoT for each incoming ext/vfs/req message."""
        path = params.get('path')
        try:
            data = self._dispatch(op, path, params)
            self._minis.ext_respond(self.EXT_TYPE, req_id, True, data=data)
        except OSError as e:
            code = _oserror_code(e)
            self._minis.ext_respond(self.EXT_TYPE, req_id, False,
                                    error={'code': code, 'message': str(e)})
        except Exception as e:
            self._minis.ext_respond(self.EXT_TYPE, req_id, False,
                                    error={'code': 'Unknown', 'message': str(e)})

    # ── Dispatch ──────────────────────────────────────────────────────────────

    def _dispatch(self, op, path, params):
        if op == 'stat':
            return self._stat(self._resolve(path))
        elif op == 'readdir':
            return self._readdir(self._resolve(path))
        elif op == 'readfile':
            return self._readfile(self._resolve(path))
        elif op == 'writefile':
            return self._writefile(
                self._resolve(path),
                params.get('data', ''),
                params.get('options') or {},
            )
        elif op == 'delete':
            return self._delete(
                self._resolve(path),
                params.get('options') or {},
            )
        elif op == 'rename':
            new_path = params.get('newPath')
            if not new_path:
                raise ValueError("rename requires 'newPath'")
            return self._rename(
                self._resolve(path),
                self._resolve(new_path),
                params.get('options') or {},
            )
        elif op == 'mkdir':
            return self._mkdir(self._resolve(path))
        else:
            raise ValueError('Unknown VFS operation: ' + repr(op))

    # ── Operations ────────────────────────────────────────────────────────────

    def _stat(self, real):
        s = os.stat(real)
        is_dir = bool(s[0] & _STAT_MODE_DIR)
        return {
            'type': _DIR if is_dir else _FILE,
            'size': s[6],
            'ctime': s[7] * 1000,
            'mtime': s[8] * 1000,
        }

    def _readdir(self, real):
        entries = []
        for name in os.listdir(real):
            child = real.rstrip('/') + '/' + name
            try:
                s = os.stat(child)
                ftype = _DIR if (s[0] & _STAT_MODE_DIR) else _FILE
            except OSError:
                ftype = _FILE
            entries.append({'name': name, 'type': ftype})
        return {'entries': entries}

    def _readfile(self, real):
        with open(real, 'rb') as f:
            raw = f.read()
        # ubinascii.b2a_base64 appends '\n' — strip it
        return {'data': ubinascii.b2a_base64(raw).decode().rstrip()}

    def _writefile(self, real, data_b64, options):
        create    = options.get('create',    True)
        overwrite = options.get('overwrite', True)
        exists    = _exists(real)
        if exists and not overwrite:
            raise OSError(17, 'File exists: ' + real)
        if not exists and not create:
            raise OSError(2, 'No such file: ' + real)
        raw = ubinascii.a2b_base64(data_b64)
        with open(real, 'wb') as f:
            f.write(raw)
        return {}

    def _delete(self, real, options):
        recursive = options.get('recursive', False)
        s = os.stat(real)
        if s[0] & _STAT_MODE_DIR:
            if recursive:
                _rmtree(real)
            else:
                os.rmdir(real)
        else:
            os.remove(real)
        return {}

    def _rename(self, old_real, new_real, options):
        if _exists(new_real) and not options.get('overwrite', False):
            raise OSError(17, 'File exists: ' + new_real)
        os.rename(old_real, new_real)
        return {}

    def _mkdir(self, real):
        os.mkdir(real)
        return {}

    # ── Path helpers ──────────────────────────────────────────────────────────

    def _resolve(self, path):
        """Resolve a VFS path to a real device path. Guards against traversal."""
        if not path:
            raise ValueError("'path' is required")
        combined = self._root + '/' + path.lstrip('/')
        parts = []
        for part in combined.split('/'):
            if part == '' or part == '.':
                continue
            elif part == '..':
                if parts:
                    parts.pop()
            else:
                parts.append(part)
        real = '/' + '/'.join(parts)
        root_prefix = self._root if self._root != '/' else ''
        if root_prefix and not (real == root_prefix or real.startswith(root_prefix + '/')):
            raise OSError(13, 'Access denied: ' + path)
        return real


# ── Module-level helpers ──────────────────────────────────────────────────────

def _exists(path):
    try:
        os.stat(path)
        return True
    except OSError:
        return False


def _rmtree(path):
    """Recursively delete a directory tree (MicroPython has no shutil)."""
    for name in os.listdir(path):
        child = path.rstrip('/') + '/' + name
        try:
            s = os.stat(child)
            if s[0] & _STAT_MODE_DIR:
                _rmtree(child)
            else:
                os.remove(child)
        except OSError:
            pass
    os.rmdir(path)


def _oserror_code(exc):
    """Map OSError errno to a VFS error code string."""
    errno = exc.args[0] if exc.args else 0
    if errno == 2:  return 'FileNotFound'
    if errno == 17: return 'FileExists'
    if errno == 20: return 'FileNotADirectory'
    if errno == 21: return 'FileIsADirectory'
    if errno == 13: return 'NoPermissions'
    return 'Unknown'
