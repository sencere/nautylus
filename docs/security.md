# Security

Nautylus is an embedded local database. Its first security boundary is the
operating-system user account that owns the database files.

## Implemented

### Encrypted database files

Snapshots can be encrypted into a separate authenticated file with the public
file APIs:

```c
ng_encrypt_file("graph.ng", "graph.ngx", password);
ng_decrypt_file("graph.ngx", "graph-restored.ng", password);
```

The CLI provides the equivalent operations:

```sh
./build/nautylus encrypt graph.ng graph.ngx 'correct horse battery staple'
./build/nautylus decrypt graph.ngx graph-restored.ng 'correct horse battery staple'
```

The encrypted envelope uses libsodium's XChaCha20-Poly1305 authenticated
encryption and Argon2id password-based key derivation. A wrong password or any
modified ciphertext is rejected before a plaintext snapshot is written. The
encrypted file has the independent `NGCRYPT1` format; existing plaintext
snapshots remain readable and are not changed automatically.

The password is not stored in the database. Prefer passing it through a secret
manager or a protected application API; command-line arguments can be visible
in process listings on some operating systems. Decryption produces a plaintext
snapshot, so protect that output with the same file permissions.

### Web workbench authentication

The local HTTP workbench supports optional HTTP Basic Authentication. Set
`NAUTYLUS_AUTH` to a `username:password` value before starting `serve`:

```sh
NAUTYLUS_AUTH='admin:change-this-password' ./build/nautylus serve graph.ng 6180
```

Alternatively, select another environment variable with:

```sh
./build/nautylus serve graph.ng 6180 --auth-env NAUTYLUS_AUTH
```

When configured, authentication is checked before static files or API routes
are served. Missing or incorrect credentials receive `401 Unauthorized`. The
credential is kept in process memory only and is never written into the graph
or snapshot. The default remains unauthenticated for backwards compatibility,
so set the variable whenever the workbench is accessible to another user.

This is request authentication, not transport encryption. The server binds to
localhost and does not implement TLS; use HTTPS through a reverse proxy or an
SSH tunnel for remote access.

On POSIX systems, Nautylus hardens files it writes by setting owner-only
permissions:

```text
0600
```

This is applied to:

* native database snapshots written by `ng_save()`;
* GraphSAGE model files;
* vector-index files;
* triple export files;
* property-graph node and relationship export files;
* query-output files produced by the binding-oriented file helpers.

The public helper `ng_secure_file(path)` applies the same owner-only permission
policy to an existing file. On non-POSIX builds it is currently a no-op that
returns `NG_OK`.

Native snapshots also include the existing format checksum and strict loader
validation. That checksum detects accidental corruption; it is not a
cryptographic authenticity check.

## Not Implemented

Nautylus does not currently provide:

* password-protected database files;
* cryptographic signatures or HMAC authentication;
* role-based access control;
* multi-user authorization;
* secure network transport.

HTTP Basic Authentication currently protects only the web workbench process. It
does not add password checks to direct C API calls, CLI commands, or arbitrary
access to the snapshot by an operating-system user who can read it.

For sensitive data today, keep database files on an encrypted filesystem or disk
volume, restrict OS-level account access, and avoid exposing the local web
workbench outside trusted local environments.

## Future Work

Good next steps would be:

* encrypted database key rotation and password-change tooling;
* authenticated snapshots using a keyed MAC;
* secure deletion guidance for temporary files;
* explicit web workbench authentication before exposing it beyond localhost.
