# QGfx MVP

QGfx is the first Citadel GPU-composition layer, not a full shader engine.

## Scope

The QGfx MVP is limited to:
- GPU surface handles for pre-rasterized window surfaces
- dirty-region uploads
- batched 2D draw operations
- present through a backend driver

The QGfx MVP does not include:
- shader pipelines
- widget-level GPU rendering
- 3D transforms
- assuming modern desktop GPU behavior under QEMU

## Core Types

- `QGfx::Surface`: CPU-to-GPU bridge for a drawable surface
- `QGfx::DrawOp`: one blit/composite operation
- `QGfx::Batch`: a frame batch of draw operations
- `QGfx::Driver`: backend interface for upload, submit, and present
- `QGfx::Context`: thin coordinator around a driver

## QEMU Constraint

QEMU is good enough to validate the architecture boundary and a conservative 2D acceleration model.
It is not sufficient evidence for modern programmable-GPU assumptions.

That means Citadel can safely pursue:
- upload discipline
- blit batching
- compositor-to-driver command flow
- conservative alpha and scaling support when the backend proves it

Current conservative VMware/QEMU backend status:
- `QGfx::VmwareSVGADriver` is valid as an architectural backend
- it currently exposes framebuffer-backed scanout uploads plus screen-to-screen rect copy
- it still does not claim offscreen surface uploads, alpha blending, or scaling

Citadel should defer:
- shader-first design
- effect-heavy rendering claims
- hardware-agnostic performance assumptions

## Intended Next Slice

1. Add a VMware SVGA-backed `QGfx::Driver` with conservative capability reporting.
2. Add an optional compositor path that emits `QGfx::DrawOp` for top-level surfaces.
3. Keep the current CPU compositor/present path as fallback until the new path is validated.
