# DHCP Completion Plan

## [X] Step 1 — DHCP client flow
  - Validate current DISCOVER/OFFER/REQUEST/ACK state machine; add handling for DECLINE and INFORM if needed.
  - Ensure REQUEST includes correct client identifier/server identifier and handles unicast responses.
  - Add REQUEST resend/backoff rules shared by renew/rebind paths to avoid duplication.

## [X] Step 2 — Lease management
  - Parse and store T1/T2 (options 58/59); fall back to 50%/87.5% defaults when missing.
  - Implement RENEWING (unicast REQUEST to server ID) and REBINDING (broadcast REQUEST) with state transitions on ACK/NAK/timeout.
  - Track lease expiration and trigger restart when exhausted; reset retry counters appropriately.
  - Emit DHCP RELEASE during shutdown or interface disable; clear network context readiness.

## [X] Step 3 — Configuration application
  - Apply subnet mask, gateway, and DNS server to network context/runtime resolver;
  - Refresh ARP and routing state after IP changes; flush stale entries when lease changes.

## [X] Step 4 — Error handling and fallback
  - Add exponential/backoff or capped retry timings; log retries with reason.
  - Handle NAK in all relevant states (REQUESTING/RENEWING/REBINDING) and restart discovery.
  - Add optional fallback to static configuration when DHCP fails after max retries.

## [ ] Step 5 — Testing and observability
  - Extend Test-Network to cover option parsing (51/53/54/58/59), state transitions, and timeout paths.
  - Add debug logs per state transition (including renew/rebind/release) and summary of applied configuration.

## Notes:
- **No magic number, use defines**
- **No code duplication**
- **Least possible dependencies**
