# vsgOcean

**An experimental ocean scene renderer for marine robotics, built on VulkanSceneGraph.**

> :warning: **Status: ongoing, experimental project.** vsgOcean is under active development: APIs will change, features are incomplete, and rendering is a baseline that will be refined. It is not ready for production use. Feedback and contributions are welcome, but expect churn.

vsgOcean renders marine scenes — terrain, surface vehicles and underwater robots — with an environment designed for simulation. The light transport is driven by physical water parameters (the Jerlov taxonomy, a bio-optical composition), the wave models are inspired by [asv_wave_sim](https://github.com/srmainwering/asv_wave_sim) (a Gazebo wave plugin), and every layer is settable at runtime — by the GUI, the command line, or the C++ API.

In short:

- **Scene model**: named, nestable `Model`s with frames addressable by name and movable at runtime through `setPose()` — the seam for driving visuals from a dynamics simulator; SDF worlds and models load natively *(optional, [sdformat](https://github.com/gazebosim/sdformat))*.
- **Sky and weather**: analytic sky with a sun-driven day/night cycle (moonless, dark nights — no ambient floors), hemisphere-dependent constellations, cloud cover on the okta scale, rain (falling streaks, splash rings, a roughened sea).
- **Ocean surface**: Gerstner component waves and an ECKV FFT sea, both wave models inspired by [asv_wave_sim](https://github.com/srmainwering/asv_wave_sim) (a Gazebo wave plugin), with a detail cascade and Jacobian whitecap foam; wind-driven ripple detail; sky reflection, glitter and glare above, the Snell window below.
- **Underwater rendering**: per-pixel, per-channel Jerlov attenuation over the true submerged view segment, Henyey-Greenstein in-scatter, an underwater sky, volumetric lamp cones, marine snow and a turbidity-driven depth-of-field blur.
- **Dimmable lights**: an SDF `<light>` intensity is each lamp's ceiling; lamps cluster by aim direction into per-robot light groups (one face, one control) — shading, cones and snow sparkle all follow.
- **Annotation ground truth**: a semantic pass renders the models with each pixel carrying its model's ID (read back per pixel, `--semantic` in the example writes the ID image and prints the derived boxes) - the base for training-data generation. At runtime the **R** key switches the view between the photo, the annotation and a 50% overlay; the display colour of a model follows its name, deterministically.
- **Render pipelines**: the scene renders from any number of cameras (sensor cameras to texture, ready for image readback); `--headless` renders offscreen without a window, for servers, CI and non-presenting GPUs.

## Videos

[![The ocean surface: the wind builds a foam-capped FFT sea while the water type steps from oceanic to coastal](https://github.com/user-attachments/assets/4b821752-acc9-4fc9-8505-bbc6754527d7)](https://github.com/user-attachments/assets/4b821752-acc9-4fc9-8505-bbc6754527d7)

[![Underwater: an ROV at a ship wreck, its dive lamps dimmed to a quarter of their SDF intensity, the water stepping from oceanic to coastal (the picture-in-picture is its own camera)](https://github.com/user-attachments/assets/f06d16ad-1c0f-4c85-80ff-6aadfeeea3d0)](https://github.com/user-attachments/assets/f06d16ad-1c0f-4c85-80ff-6aadfeeea3d0)

*(still frames: [`img/surface.png`](img/surface.png), [`img/underwater.png`](img/underwater.png))*

## Core rendering: the water colour and the waves

The look lives in two parameter families, both runtime-tunable.

### Water colour and light transport

The water is described either by a **Jerlov water type** (the classical marine-optics taxonomy: oceanic I, IA, IB, II, III, then coastal 1–9 — open ocean to murky harbour) or directly by its **bio-optical composition**: chlorophyll [mg/m³], coloured dissolved organic matter (CDOM absorption at 440 nm [1/m]) and suspended sediment [g/m³]. The composition drives a simple bio-optical model producing the per-channel diffuse attenuation, the scattering coefficient and the sub-surface colour, from which everything follows: the underwater attenuation and in-scatter (per-channel Beer–Lambert along the true submerged view segment, Henyey–Greenstein forward scattering g = 0.924), the water's own colour seen from above, the depth-of-field blur and the lamp cones. The 14 Jerlov types are presets of the composition space; the default is coastal 3 (the western English Channel).

### Waves

The default sea is a **Gerstner component wave field**, a wave model inspired by [asv_wave_sim](https://github.com/srmainwering/asv_wave_sim) (a Gazebo wave plugin): it follows its component spreading (8 octave-spaced components at 36°), giving an irregular, short-crested sea. The mean component (amplitude, period, steepness), the spreading (count, scale, angle) and the wind are all runtime parameters, and the field can also be evaluated on the CPU (`Wavefield::height`), not only rendered. With `--foam` the component sea grows **Jacobian whitecaps** too: the component field never truly folds, so the crest compression is remapped by a cubic wind factor — the coverage rises steeply with the wind, from nothing at Beaufort 2–3 to broad whitecap streaks near gale.

The alternative **FFT sea** (`--fft`) synthesises an ECKV unified directional spectrum by inverse FFT, after the linear random FFT model of asv_wave_sim, with a choppiness parameter (horizontal displacement whose folds produce **Jacobian whitecap foam** with `--foam`), a wave-age parameter and an optional second, finer cascade (`--fft-detail`) that kills tile repetition and adds detail.

### References

- N. G. Jerlov, *Marine Optics*, Elsevier, 1976 — the water type taxonomy.
- L. de Gerstner (1809), *Theorie der Wellen* — the trochoidal wave model.
- W. J. Pierson & L. Moskowitz, *A proposed spectral form for fully developed wind seas…*, J. Geophys. Res., 1964 — the wind-sea spectrum asv_wave_sim samples.
- T. Elfouhaily, B. Chapron & D. Katsaros, *A unified directional spectrum for long and short wind-driven waves*, J. Geophys. Res., 1997 — the ECKV spectrum of the FFT sea.
- J. Tessendorf, *Simulating Ocean Water*, 2001 — the inverse-FFT synthesis and the Jacobian foam.
- L. C. Henyey & J. L. Greenstein, *Diffuse radiation in the galaxy*, ApJ, 1941 — the forward-scattering phase function.
- [asv_wave_sim](https://github.com/srmainwering/asv_wave_sim) (R. Mainwaring) — the component wave and linear random FFT models vsgOcean's wave fields are inspired by.

## The example

The example application renders an islands scene with a surface vehicle (a WAM-V), an ROV (BlueROV2) circling a ship wreck with its dive lamps, and a floating wind turbine — animated in real time on the waves, with the environment fully tunable at runtime. Two sensor cameras (the ROV's nose camera and a boat camera) render to texture and preview in the panel.

```bash
# default: islands + wamv + bluerov2 + wreck, static sun (35 deg, 135 deg azimuth) and waves
./vsgOcean_example

# a few looks
./vsgOcean_example --sun-cycle                                # animate the sun (1 simulated hour per second; Space pauses)
./vsgOcean_example --fft --fft-detail --foam 0.7 --wind 14 45 # a wind-built, foam-capped FFT sea
./vsgOcean_example --clouds 7 --rain 0.8                     # a downpour: streaks, splash rings, a roughened sea
./vsgOcean_example --light bluerov2/lights1 0.25              # the ROV's dive lamps to 25% of their SDF intensity
./vsgOcean_example --latitude -35 --longitude 18             # the southern hemisphere: Crux replaces the Dipper

# headless: render offscreen (no window; for servers, CI, non-presenting GPUs) and write a PNG
./vsgOcean_example --headless --extent 1920 1080 -f 60 --screenshot shot.png

# the semantic ID image + the derived bounding boxes (training-data ground truth)
./vsgOcean_example --headless --extent 1280 720 -f 60 --screenshot shot.png --semantic

# custom scenes: pass SDF files as positional arguments
./vsgOcean_example my_world.sdf
```

### Saving and reloading an environment

An environment tuned in the widgets is worth keeping: **E** exports the current environment to a YAML file — sky and weather, wind, water optics and wave. **`--env`** loads it back at startup, and every group and key is optional, so hand-written files can be partial:

```yaml
# storm.yaml, by someone in a hurry
wind:
  speed_m_s: 16
sky:
  cloud_cover_oktas: 7
  rain_rate: 0.8
waves:
  model: fft
  foam: 0.6
```

```bash
./vsgOcean_example --env storm.yaml
```

The file is applied first, so any explicit command line flag still overrides its key (`--env storm.yaml --rain 0` for a dry storm). The full key list is documented in [`include/vsgOcean/core/EnvironmentFile.h`](include/vsgOcean/core/EnvironmentFile.h); the support is built when [yaml-cpp](https://github.com/jbeder/yaml-cpp) is found (a common system package).

### Runtime controls

The environment is tuned at runtime through the keyboard and widgets (`vsgOcean::EnvironmentControls`, a library feature). The keys are deliberately few — the tuning lives in the widgets:

| input | action |
| --- | --- |
| left drag / right drag / wheel | translate / orbit (stable horizon) / dolly |
| H | help window on / off (the bindings, the model tracking, the live stats) |
| C | camera previews on / off (hidden by default; says so when no camera is rendering) |
| S | sky widget on / off (sun, day cycle, fog, cloud cover, rain) |
| O | ocean surface widget on / off (wind, waves) |
| U | underwater widget on / off (water optics, current, snow, DOF blur) |
| L | lights widget on / off (one intensity slider per light group) |
| R | semantic view: photo, the annotation (each model in the colour of its name), the annotation at 50% over the photo |
| E | export the environment (a flat YAML file) |
| Tab | cycle the tracked model (camera follows) |
| Esc | quit |

The bindings are always visible in a small HUD in a corner of the window. When vsgOcean is built with vsgImGui, each widget is its own window: the widgets read the environment every frame and write only on change — an update from outside (a topic, the code) is reflected immediately, and changing a widget takes over from any preset. The underwater widget carries the **depth-of-field blur** (off by default): the water's turbidity blurs the view with the water path length, following the Jerlov beam attenuation.

## The rest of the command line

The few flags shown above cover the common looks; the example has many more (capture, diagnostics, every wave and water parameter). Run it with `--help` for the full list of the vsgOcean flags — and the standard vsg viewer flags (`--window`, `--gpu`, `--mt`, `--log-level`, …) also apply.

## Building

Dependencies:

- C++17, CMake ≥ 3.22
- [VulkanSceneGraph](https://github.com/vsg-dev/VulkanSceneGraph) (vsg) ≥ 1.1
- [vsgXchange](https://github.com/vsg-dev/vsgXchange) (model loading)
- [sdformat](https://github.com/gazebosim/sdformat) ≥ 16 *(optional)* — SDF support is enabled automatically when sdformat is found; build with `-DVSGOCEAN_WITH_SDF=OFF` to force it off
- [vsgImGui](https://github.com/vsg-dev/vsgImGui) *(optional)* — the environment tuning GUI is enabled automatically when vsgImGui is found; build with `-DVSGOCEAN_WITH_GUI=OFF` to force it off
- [yaml-cpp](https://github.com/jbeder/yaml-cpp) *(optional)* — the YAML environment files (the E export / `--env` load) are enabled automatically when yaml-cpp is found; build with `-DVSGOCEAN_WITH_ENV_FILE=OFF` to force it off
- A Vulkan-capable GPU and driver

```bash
cmake -B build .
cmake --build build -j
./build/example/vsgOcean_example
```

## Roadmap

- Underwater rendering refinements
    - god rays
    - wave-field caustics on geometry
    - depth-consistent scene lighting (the light on the geometry attenuated by its depth)
- Surface refinements: planar reflection / refraction passes of the actual scene; foam persistence; subsurface scattering; screen-space reflections; exposure / tone mapping; sun shadow map on the water
- Middleware integration is kept out of this library on purpose — poses and environment updates come from the outside, through the C++ API

## Acknowledgments and third-party notices

vsgOcean is MIT-licensed. The following third-party work is included or derived, with grateful acknowledgment:

- **[asv_wave_sim](https://github.com/srmainwering/asv_wave_sim)** (© 2019 Rhys Mainwaring, [Apache-2.0](http://www.apache.org/licenses/LICENSE-2.0)): the `Wavefield` wave model (Gerstner component waves, Pierson–Moskowitz sampling, deep water dispersion) is derived from `asv_wave_sim`, with modifications.
- **[UUV Simulator](https://github.com/uuvsimulator/uuv_simulator)** (© 2016 The UUV Simulator Authors, [Apache-2.0](http://www.apache.org/licenses/LICENSE-2.0)): the Gerstner surface displacement and normal formulation in `shaders/ocean.vert` is derived from its wave shaders, with modifications.
- **[VulkanSceneGraph](https://github.com/vsg-dev/VulkanSceneGraph)**, **[vsgXchange](https://github.com/vsg-dev/vsgXchange)** (© Robert Osfield, MIT) and **[sdformat](https://github.com/gazebosim/sdformat)** (© Open Source Robotics Foundation, Apache-2.0): the rendering and scene description stack vsgOcean builds on.
- The interleaved gradient noise dithering used in the shaders follows the technique of Jorge Jimenez, *"Next Generation Post Processing in Call of Duty: Advanced Warfare"* (2014).
- Example models: **[VRX / WAM-V](https://github.com/osrf/vrx)** (© OSRF, Apache-2.0) for the aquabot, **[bluerov2_description](https://github.com/bluerov2_description)** (MIT) for the BlueROV2, and the M/S Herkules ship wreck from **[UUV Simulator](https://github.com/uuvsimulator/uuv_simulator)** (© 2016 Bosch / The UUV Simulator Authors, Apache-2.0).

A large part of the development was carried out with agentic AI coding assistants: **[GitHub Copilot](https://github.com/features/copilot)** and **[Mistral Vibe](https://mistral.ai)** (Mistral AI's agentic CLI coding agent).

Projects consulted as rendering references but **not** included: [osgOcean](https://github.com/kbale/osgOcean) (LGPL-3), [Stonefish](https://github.com/patrykcieslak/stonefish) (GPL-3), [Vulkan-Spectral-Ocean](https://github.com/micaalle/Vulkan-Spectral-Ocean) (MIT).

Per-file attribution is kept in the headers of the derived sources (`src/core/Wavefield.cpp`, `shaders/ocean.vert`).
