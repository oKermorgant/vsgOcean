# vsgOcean

**An experimental ocean scene renderer for marine robotics, built on VulkanSceneGraph.**

> :warning: **Status: ongoing, experimental project.** vsgOcean is under active development: APIs will change, features are incomplete, and rendering is a baseline that will be refined. It is not ready for production use. Feedback and contributions are welcome, but expect churn.

vsgOcean renders marine scenes — terrain, surface vehicles and underwater robots — with an environment designed for simulation:

- **Scene model**: a tree of named, nestable `Model`s rooted at a `world` model, holding meshes under fixed or dynamic transforms. Frames are addressable by name and movable at runtime through `setPose()` — the seam for driving visuals from a dynamics simulator.
- **SDF loading** *(optional, requires [sdformat](https://github.com/gazebosim/sdformat))*: SDF worlds and models become the scene model, with poses resolved through SDF semantic poses, `package://` / `model://` / `file://` URI resolution, and mesh scales.
- **Environment**: sun position (elevation/azimuth), fog, wind, geolocation and Jerlov water types — all with reasonable defaults and settable at runtime. Night is moonless and genuinely dark: every light path (sky, fog, in-scatter, scene lighting) is sun-driven, so an unlit night scene is lit only by its own lamps.
- **Sky**: analytic shader sky (day/night cycle from the sun elevation, constellations at night), with scene lighting that follows the sun. The night sky follows the hemisphere: the northern sky carries Orion, the Big and Little Dippers (Ursa Major and Minor), Cassiopeia and Taurus; the southern sky carries Crux (the Southern Cross), Centaurus, Carina, Scorpius and Sirius.
- **Ocean surface**: a Gerstner wave field using the **same wave model as Gazebo** ([asv_wave_sim](https://github.com/srmainwering/asv_wave_sim)), so the visual waves and a dynamic wave field in a physics simulation can be synchronised. The CPU `Wavefield` evaluation and the GPU displacement are the same model. The default sea follows asv_wave_sim's component spreading (8 octave-spaced components at 36°), giving an irregular, short-crested sea; the component count, scale and angle are tunable (GUI sliders or `--wave-*`). Wind-driven ripple detail is layered on top as a drifting noise field. The surface shades around the shared analytic sky in both directions: above, Fresnel-weighted sky reflection (fading in toward the horizon), sun glitter and glare, and the transmitted Jerlov body colour; from below, the Snell window with total internal reflection beyond the critical angle. The composite applies FXAA anti-aliasing.
- **Underwater rendering**: when the camera dives below the waves, the scene is composited underwater — per-pixel, per-channel Jerlov light attenuation following the true submerged view segment (red is absorbed first), Henyey-Greenstein forward in-scatter (g = 0.924) with a Petzold-family scattering table under the Jerlov taxonomy, Jerlov-filtered lighting and an underwater sky — all driven by the water type.
- **Volumetric lamp cones**: the scene's point and spot lights are ray-marched as visible single-scattering cones through the water column — the night wreck scene is lit only by the ROV's dive beams, with the cones readable against open water.
- **Marine snow**: drifting particles in a box wrapped around the camera, advected by the water current (`setWaterCurrent()` — the seam for a dynamics-driven current topic) and reflecting the light that reaches them (the sun surviving to their depth, and the lamps — snow sparkles in the beams at night). Density is tunable up to 10x (`--snow 0..10`, default 0.6, the `N`/`M` keys, the GUI slider).
- **SDF lights**: `<light>` elements (point, spot with cone aperture and range, directional) become scene lights attached to their frame, so vehicle lamps follow the vehicle.
- **Multiple render pipelines**: the scene renders from any number of cameras — the camera state flows through push constants, so the shared sky, surface and models are correct from every view. Sensor cameras render to texture with their own composite (the example previews an ROV underwater camera and a boat surface camera in its panel); the output image is ready for readback to an image topic, and the composited image can be written to a PNG (`--screenshot`).
- **Headless rendering**: `--headless` renders offscreen without a window or swapchain and writes the screenshot — for servers and CI, and for GPUs that can render but not present (e.g. over a forwarded X connection).

The example application renders an islands scene with a surface vehicle (aquabot), an ROV (BlueROV2) diving around a ship wreck, animated in real time, with a day/night cycle (opt-in) and moving waves.

## Building

Dependencies:

- C++17, CMake ≥ 3.22
- [VulkanSceneGraph](https://github.com/vsg-dev/VulkanSceneGraph) (vsg) ≥ 1.1
- [vsgXchange](https://github.com/vsg-dev/vsgXchange) (model loading)
- [sdformat](https://github.com/gazebosim/sdformat) ≥ 16 *(optional)* — SDF support is enabled automatically when sdformat is found; build with `-DVSGOCEAN_WITH_SDF=OFF` to force it off
- [vsgImGui](https://github.com/vsg-dev/vsgImGui) *(optional)* — the environment tuning GUI is enabled automatically when vsgImGui is found; build with `-DVSGOCEAN_WITH_GUI=OFF` to force it off
- A Vulkan-capable GPU and driver

```bash
cmake -B build .
cmake --build build -j
./build/example/vsgOcean_example
```

## The example

```bash
# default: islands + aquabot + bluerov2 + wreck, static sun (35 deg, 135 deg azimuth) and waves
./vsgOcean_example

# customise
./vsgOcean_example --sun-cycle                     # animate the sun (1 simulated hour per second; Space pauses)
./vsgOcean_example --sun 20 200                    # static sun at 20 deg elevation, azimuth 200
./vsgOcean_example --wind 8 45 --wave-amplitude 0.2 --wave-period 6
./vsgOcean_example --wave-number 4 --wave-scale 1.5 --wave-angle 20   # regular, long-crested sea
./vsgOcean_example --jerlov 8                      # turbid coastal water
./vsgOcean_example --fog 0.01                      # haze (default 0.02, scaled 0..1)
./vsgOcean_example --snow 8 --current 0.5 0        # heavy marine snow (up to 10), drifting East
./vsgOcean_example --latitude -35 --longitude 18   # the southern hemisphere: Crux replaces the Dipper
./vsgOcean_example --debug --eye 12 5 -40          # validation layers, camera near the wreck
./vsgOcean_example --eye 12 5 -40 --center 12 0 -48   # fully deterministic view

# headless: render N frames offscreen (no window) and write a PNG
./vsgOcean_example --headless -f 20 --sun 35 135 --screenshot shot.png
./vsgOcean_example --headless --extent 1920 1080 -f 60 --screenshot shot.png

# custom scenes: pass SDF files as positional arguments
./vsgOcean_example my_world.sdf
```

### Runtime controls

The environment can be tuned at runtime (`vsgOcean::EnvironmentControls`, a library
feature also usable headless through the keyboard):

| input | action |
| --- | --- |
| left drag / right drag / wheel | translate / orbit (stable horizon) / dolly |
| Up, Down | sun elevation |
| Left, Right | sun azimuth |
| Page Up, Page Down | sea level |
| A / D, Q / E, W / S | wave amplitude, wave period, wind speed |
| Z / C | Jerlov water type |
| N / M | marine snow density |
| [ / ] | water current speed |
| P | ocean surface on / off |
| Space | day cycle animation on / off |
| Tab | cycle the tracked model (camera follows) |
| V | camera preview widget on / off (shown by default) |
| H | sliders panel on / off (hidden by default) |

The key bindings are always visible in a small HUD in a corner of the window. When
vsgOcean is built with vsgImGui, the **H** key also toggles a panel with the same
parameters as sliders plus a dropdown selecting the tracked model; the font for the
HUD is loaded from the system (override with the `VSGOCEAN_FONT` environment
variable).

## Roadmap

- Underwater rendering refinements
    - god rays
    - wave-field caustics on geometry
- Surface refinements: planar reflection / refraction passes of the actual scene
- Spectral (FFT) wave synthesis on top of the same wave-field interface
- A separate ROS wrapper package (tf-driven poses, image topics)
- A separate Gazebo rendering plugin (Gazebo-driven poses, image topics)

## Acknowledgments and third-party notices

vsgOcean is MIT-licensed. The following third-party work is included or derived, with grateful acknowledgment:

- **[asv_wave_sim](https://github.com/srmainwering/asv_wave_sim)** (© 2019 Rhys Mainwaring, [Apache-2.0](http://www.apache.org/licenses/LICENSE-2.0)): the `Wavefield` wave model (Gerstner component waves, Pierson–Moskowitz sampling, deep water dispersion) is derived from `asv_wave_sim`, with modifications.
- **[UUV Simulator](https://github.com/uuvsimulator/uuv_simulator)** (© 2016 The UUV Simulator Authors, [Apache-2.0](http://www.apache.org/licenses/LICENSE-2.0)): the Gerstner surface displacement and normal formulation in `shaders/ocean.vert` is derived from its wave shaders, with modifications.
- **[VulkanSceneGraph](https://github.com/vsg-dev/VulkanSceneGraph)**, **[vsgXchange](https://github.com/vsg-dev/vsgXchange)** (© Robert Osfield, MIT) and **[sdformat](https://github.com/gazebosim/sdformat)** (© Open Source Robotics Foundation, Apache-2.0): the rendering and scene description stack vsgOcean builds on.
- The interleaved gradient noise dithering used in the shaders follows the technique of Jorge Jimenez, *"Next Generation Post Processing in Call of Duty: Advanced Warfare"* (2014).
- Example models: **[VRX / WAM-V](https://github.com/osrf/vrx)** (© OSRF, Apache-2.0) for the aquabot, **[bluerov2_description](https://github.com/bluerov2_description)** (MIT) for the BlueROV2, and the M/S Herkules ship wreck from **[UUV Simulator](https://github.com/uuvsimulator/uuv_simulator)** (© 2016 Bosch / The UUV Simulator Authors, Apache-2.0).

A large part of the development was carried out with **[Mistral Code](https://mistral.ai)**, Mistral AI's agentic coding assistant.

Projects consulted as rendering references but **not** included: [osgOcean](https://github.com/kbale/osgOcean) (LGPL-3), [Stonefish](https://github.com/patrykcieslak/stonefish) (GPL-3), [Vulkan-Spectral-Ocean](https://github.com/micaalle/Vulkan-Spectral-Ocean) (MIT).

Per-file attribution is kept in the headers of the derived sources (`src/Wavefield.cpp`, `shaders/ocean.vert`).
