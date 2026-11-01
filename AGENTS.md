# Agent instructions — vsgOcean

- Notify at every finished task (~2 min+), at every blocker, AND when a
  rabbit hole / dead end is discovered (a long investigation that turned out
  wrong, a theory ruled out, a fix that did not work) - the user decides whether
  to redirect, and dead-end knowledge is worth a ping too. This is the FINAL action:
  - When the user is AT the PC (a live session, recent interaction): `~/notify-pc "Vibe: vsgOcean" "<Done | Blocked | Dead end>: <summary>"` (pops a dialog on the local screen + a line in the terminals). Do NOT ping the phone then.
  - When the user is AWAY (idle session, overnight, they said they are leaving): `~/notify-phone "Vibe: vsgOcean" "<Done | Blocked | Dead end>: <summary>"`.
- This is a sandboxed account; auto-approve is intended. Do not push to remotes.
