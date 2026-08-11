---
status: accepted
---

# Adopt protected, memory-capacity-driven model residency

Lemonade will use a server-owned residency policy that plans GPU model admission and pressure reclamation against residency memory domains rather than relying only on per-type model counts. Runtime pins veto automatic hard reclamation, in-use state vetoes automatic reclamation, soft reclamation may preserve weights, backend process, and pin, and NPU/FLM exclusivity defines conflicts without granting newcomers precedence over protected residents. Hatchery's GTT-backed memory domain is the first behavioral reference profile and initial validation target; portable adapters report topology, signals, and capabilities to the common policy. Fallback-only combinations use their declared conservative fallback, while unsupported combinations refuse because no safe residency behavior exists. This trades implementation complexity for predictable residency, cooperation with external GPU workloads, and one many-clients-one-server lifecycle truth.
