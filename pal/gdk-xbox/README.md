# pal/gdk-xbox/ (deferred — gated flip)

The full-metal console backend (`Gaming.Xbox.Scarlett.x64`): mostly a copy of
`gdk-desktop` plus the Xbox Graphics/Media/Storage extensions, with lifted memory /
feature-level targets and optional DirectStorage. **Gated** behind ID@Xbox + GDKX + a
Business Partner Center account — **deliberately deferred** to the last responsible moment
(ADR-0004). The residual risk that lives *here* (not in the PAL): whether the stock
ORT-GenAI stack loads on the Xbox Game OS. Empty until that step is actually started.
