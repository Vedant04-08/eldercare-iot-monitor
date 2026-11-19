ELDERCARE CASE v1 — THIN FIT-CHECK PROTOTYPE
All dimensions in mm. Print at 100% scale, with slicer units set to mm.

FILES
case_body.stl and case_lid.stl: separate printable parts, already flat on Z=0.
case_body.step and case_lid.step: editable solid CAD exchange files.
build_case.py: parameterized CadQuery source; preview also requires matplotlib and Inkscape.
case_preview.png: geometry preview and coordinate drawing.
validation.json: CAD solid checks. mesh_validation.json: STL checks.

CONFIRMED GEOMETRY
Internal rectangular volume: 41 x 31 x 13, no further cavity padding.
USB ports occupy the 41 x 13 side. Opposite side has buzzer/switch lead holes.
Sensor window is exactly 6 x 3.3; its starting corner is x=29, y=7.
Buzzer pair: 8 centre-to-centre. Switch row: 3.2 centre-to-centre.
Outer shell: 45 x 35 x 15 (2 mm walls, 1 mm base and 1 mm lid).
Total with integral strap loops: 59 x 35 x 15.
External rounded corners: radius 2. Strap slots: 21 x 3 for a 20 mm strap.
Walls add to the outside dimensions; they do not consume the 41 x 31 cavity.

REFERENCE SYSTEM — IMPORTANT
Look at the layout drawing: x runs along the 41 mm direction.
The USB wall is y=0, and y increases into the cavity to 31.
The inside of the chest-facing floor is z=0; lid underside is z=13.
All positions refer to INTERNAL edges, not the outside of the printed shell.
The STL is translated onto the print bed; use the preview for measurements.
Align the assembly to these reference edges before checking openings; do not
automatically centre the smaller 38 x 29 electronics assembly in the cavity.

PROVISIONAL DIMENSIONS — VERIFY BEFORE THE FULL PRINT
The video establishes layout but cannot establish missing sub-millimetre sizes.
USB-C: assumed socket envelope begins x=6,z=0, width 9,height 4.
Charge USB: assumed envelope begins x=27,z=1, width 8,height 4.
Each opening adds 0.5 per side: USB-C opening x=5.5..15.5,z=-0.5..4.5;
charge opening x=26.5..35.5,z=0.5..5.5. These are deliberate assumptions,
not measured connector specifications. Check actual cable moulding clearance
and socket recess through the 2 mm wall. The charge connector type is unverified.
Sensor opening has NO added clearance, per requested dimensions. Test whether
the actual optical package can reach the skin-facing surface, z=-1. Sensor
height is unknown, so flush contact is NOT guaranteed. Adjust its nonconductive
support or the floor design after fitting; do not force the PCB or battery.
Sensor readings on the chest are not validated by this mechanical design.
Button: provisional 6.4 x 6.4 through-opening in the outward lid, x=28.8,y=14.8.
It is opposite the skin-facing surface to remain accessible. The 6 x 6 button
body needs a separate internal support; height and actuator travel are unknown.
No unsupported plunger or guessed button retention feature is included.
Buzzer hole centres: x=7 and 15, z=6.5, on the opposite wall y=31.
Switch hole centres: x=25,28.2,31.4, z=6.5, same wall.
All five nominal pin holes are diameter 1.2 for the reported 0.7 mm leads.
Only the pitches were measured. Group locations are designer-selected;
external body overlap must be checked. These holes do not retain components.
Secure external buzzer/switch bodies separately and insulate exposed terminals.

PRINT AND ASSEMBLY
Suggested starting slicer setup: 0.2 layer, 0.4 nozzle, 4 perimeters,
5 top/bottom layers and 25% infill. Both pieces lie flat as supplied.
PETG for the intended enclosure, or PLA for an initial fit check.
The USB roofs span up to 10 mm. Inspect bridge quality in the slicer; use
local removable supports if the college printer cannot bridge them cleanly.
Do not globally support the whole cavity. Small horizontal lead holes may
need careful drilling to 1.2 after printing, with all electronics removed.
Use four M1.6 x 5 mm small pan-head screws. Lid holes are 1.8 clearance;
body holes are 1.2 pilot x 5 deep for carefully cut/tapped M1.6 threads in plastic.
Thin lid and floor are more flexible than the original design; avoid overtightening.
Test tapping one pilot; do not force a screw into a tight printed hole.
No heat-set inserts are included. Tighten lightly; repeated servicing may wear
plastic threads. If needed, revise for inserts after confirming the hardware.
Check fit first with electronics unpowered. Use insulated supports to hold the
boards and motion sensor steady. Do not use the lid to compress the LiPo cell.
The shell has no measured custom PCB mounts or battery clamp.
Route wiring away from screw tips and the optical opening.
Use a 20 mm strap through the two side loops; no neck cord fitting is provided.
Do not route the strap over the sensor opening. Smooth skin-facing printed edges.

VALIDATION LIMITS
CAD validity, single-solid construction, no lid/body volumetric overlap,
and closed STL manifold geometry are checked digitally.
Physical fit, print tolerances, retention, skin contact, and cable insertion
remain untested. This is a first prototype, not a verified production enclosure.
