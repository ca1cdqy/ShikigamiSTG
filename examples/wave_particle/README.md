# Boundary of Wave and Particle Demo

This small recording demo creates a wave-and-particle inspired pattern with
public ShikigamiSTG APIs. The complete pattern is the single short
`emitWaveParticle` function in `src/wave_particle.h`; the rest of the example
is only an SDL3 presentation shell.

The pattern emits five equally spaced directions every tick. The original 1.5
speed is scaled to 2.5 world units per tick for this demo's larger playfield.
The group rotates clockwise with persistent angular acceleration. It is an
original approximation intended to demonstrate the API and contains no original
game assets or ECL data.

## Build And Run

```powershell
xmake build wave_particle
xmake run wave_particle
```

Press `Space` to pause, `R` to restart the deterministic session, and `Esc`
to exit.
