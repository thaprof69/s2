# SomaBridge-s2 provenance and boundary

This repository is the separate GPL-3.0 component used by SomaBridge for
Spooky2/GeneratorX communication.

- Upstream: `https://github.com/calum74/s2`
- Fork: `https://github.com/thaprof69/s2`
- Fork starting commit: `2dbc666abcf29d490e3e3bcf4826af3683a57d83`
- License: GPL-3.0, preserved from upstream
- Boundary executable: `somabridge-s2-service`
- Protocol: version 1 JSON Lines over inherited stdin/stdout
- Diagnostics: stderr only

The service owns Spooky2 discovery, serial command encoding, communication,
acknowledgement handling and output-state reporting. The proprietary SomaLofi
repository does not contain or link the GPL implementation. It starts this
program as a separate process and exchanges canonical JSON messages.

`physical_dispatched`, `acknowledged`, and `output_state` are separate fields.
A failed stop returns `output_state: unknown`, latches `rearm_required`, and
prevents configure/start until an explicit re-arm.
