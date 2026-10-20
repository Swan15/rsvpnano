# T3 agent pets on RSVP Nano

The original Waveshare LCD 3.49 can display animated pets for T3 threads running
on a VPS. This fork includes a read-only Python bridge and a Device > Agent pets
screen. Reading, themes, and focus timers remain available.

## Personal device setup

1. Flash the new **agent-pets** full firmware image for LCD 3.49 rev1. The earlier
   upstream-integration image does not include this feature.
2. Copy `config/agents.toml` and `config/agent-ca.pem` from your private setup bundle
   into `/config` on the SD card. These are separate from `/config/settings.toml`.
3. Save your home Wi-Fi network in Settings > Network (or using the companion).
4. Open Device > Agent pets. Use the page button to cycle through groups of four.
   Back, BOOT, or a short PWR press returns to Device and stops the connection.

The personal setup bundle contains a device credential. Keep it private. The
firmware contains no Wi-Fi password, T3 credential, or device token. Do not put
real tokens in source control; [agents.toml.example](agents.toml.example) contains
only placeholders. The CA file contains public trust roots, not a private key.

Agent-pets mode stays awake while selected; USB power is useful for a desk display.
Manual standby and power-off still work. Network requests run in a separate task
so the UI can animate while the VPS responds. Exiting joins that task before
another feature reuses Wi-Fi.

## Display behavior

- Working: green typing pet.
- Needs input: amber pet with an attention mark.
- Finished: blue celebrating pet.
- Failed/interrupted: red pet with an attention mark.
- Idle/queued: purple resting pet.

The feed contains up to eight threads, prioritizing pending user requests, running
work, failures, and then recently completed/idle threads. Archived/deleted threads
and settled terminal threads are omitted. Every card shows its task title and
project. These are thread statuses, not a percentage estimate of task completion.

The Nano polls about every two seconds. A failed request marks the view OFFLINE;
previous data stays visible as LAST KNOWN UPDATE with frozen pets. A feed whose
source timestamp stops changing becomes stale after 15 seconds, even if HTTP
requests still succeed. A fresh empty feed says there are no active agents.

## VPS bridge

The configured VPS is `ubuntu@chermes-router` over Tailscale SSH. Its user service
is `rsvpnano-agent-pets.service`, listening on `127.0.0.1:3948`.

The bridge opens `~/.t3/userdata/statev2.sqlite` using SQLite read-only mode and
query-only connections. It reads thread/project/run/request metadata, never
conversation messages, tool arguments, or credentials. The indexed latest-run
query has a two-second limit, and connections close after each snapshot.
Thread IDs are converted to stable SHA-256 identifiers so long T3 IDs with shared
prefixes remain distinct on the device.

T3 remains on its existing service. The bridge makes no T3 control requests and
only checks that the local T3 listener is reachable. If T3 or its database is
unavailable, the bridge returns 503 instead of a cached live-looking response.
T3's internal projection schema can change across updates; a schema change also
fails closed with 503 until the adapter is updated.

The Nano has Wi-Fi but no Tailscale client. On this VPS the small status feed is
reachable through an existing public HTTPS host at `/nano-pets/v1/status`, with a
separate 64-character device token. T3's private tailnet endpoint is unchanged.
The route rejects writes and requires the token for status reads. TLS hostname
and certificate verification remain enabled on the Nano. No redirects are followed.

For a different installation, a tailnet-only endpoint works if the Nano's Wi-Fi
network has a router providing the required Tailscale route; otherwise use an
HTTPS reverse proxy for only the authenticated status feed.

Relevant files:

- `tools/agent_pets/bridge.py`: Python 3.10+ server, standard library only.
- `tools/agent_pets/rsvpnano-agent-pets.service`: example user service.
- `tools/agent_pets/nginx-location.conf`: exact-path reverse proxy example.
- `docs/agents.toml.example`: device configuration format.

Service checks on the VPS:

```sh
systemctl --user status rsvpnano-agent-pets.service
journalctl --user -u rsvpnano-agent-pets.service --since '10 minutes ago'
```

To disable the feed, stop/disable its user service and remove its exact nginx
location, validate with `nginx -t`, then reload nginx. Rotate the device credential
by replacing `~/.config/rsvpnano-agent-pets/token`, restarting the bridge, and
updating the token in the SD config. Never print the token into diagnostic logs.

## Validation

```sh
python3 -m unittest discover -s tools/agent_pets -p 'test_*.py'
pio test -e native_test
pio run -e waveshare_esp32s3_touch_lcd_349_rev1
```

Host checks cover normalization, authentication, read-only behavior, source
failure, malformed/bounded snapshots, stale timestamps, clock wraparound,
configuration validation, display bounds, and live/offline animation. VPS checks
exercise its real T3 database and HTTPS route with the same trust roots used by
the device. Physical screen, Wi-Fi, NTP, and touch behavior still require flashing
the actual Nano; a successful host build does not validate those on hardware.
