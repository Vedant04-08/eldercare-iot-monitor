"""ElderCare fit-check enclosure. Units mm. Requires CadQuery and matplotlib."""
from pathlib import Path
import json
import subprocess
import cadquery as cq
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d.art3d import Poly3DCollection

OUT=Path(__file__).resolve().parent
L,W,H=41.,31.,13.
WALL,FLOOR,LID=2.,1.,1.
# Reference: cavity x=0..41, y=0..31, z=0..13. USB wall y=0.
# CONFIRMED optical window lower-left (29,7), 6 x 3.3.
SENSOR=(29.,7.,6.,3.3)
# ASSUMED socket envelopes, with 0.5 mm clearance per side.
# x start, z bottom, width, height (opening, not measured socket).
USB_C=(5.5,-0.5,10.,5.)
USB_CHARGE=(26.5,0.5,9.,5.)
SCREWS=[(-1.,2),(-1.,29),(42.,2),(42.,29)]

def box(x,y,z,dx,dy,dz):
    return cq.Workplane('XY').box(dx,dy,dz,centered=(False,False,False)).translate((x,y,z))
def cylinder_z(x,y,z,r,h):
    return cq.Workplane('XY').circle(r).extrude(h).translate((x,y,z))
def through_y(x,z,r):
    return cq.Workplane(cq.Plane(origin=(x,30,z),xDir=(1,0,0),normal=(0,1,0))).circle(r).extrude(5)

body=box(-WALL,-WALL,-FLOOR,L+2*WALL,W+2*WALL,H+FLOOR).edges('|Z').fillet(2)
body=body.cut(box(0,0,0,L,W,H+1))
sx,sy,sw,sh=SENSOR
body=body.cut(box(sx,sy,-3,sw,sh,4))
for x,z,w,h in (USB_C,USB_CHARGE):
    body=body.cut(box(x,-4,z,w,5,h))
# External components on opposite wall: all pin bores 1.2 mm nominal.
for x in (7,15,25,28.2,31.4):
    body=body.cut(through_y(x,6.5,0.6))
# Integral strap loops along short ends, supported on bed, 21 x 3 mm slots.
for x in (-9,42):
    loop=box(x,2,-1,8,27,2).edges('|Z').fillet(1.5)
    slotx=-6 if x<0 else 44
    loop=loop.cut(box(slotx,5,-3,3,21,5))
    body=body.union(loop)
for x,y in SCREWS:
    body=body.cut(cylinder_z(x,y,8,0.6,6))
body=body.clean()

lid=box(-WALL,-WALL,13,L+2*WALL,W+2*WALL,LID).edges('|Z').fillet(2)
for x,y in SCREWS:
    lid=lid.cut(cylinder_z(x,y,12,0.9,4))
# Accessible button aperture in outward-facing lid; position and size provisional.
lid=lid.cut(box(28.8,14.8,12,6.4,6.4,4)).clean()

parts={'case_body':body.translate((9,2,1)), 'case_lid':lid.translate((2,2,-13))}
report={}
for name,part in parts.items():
    shape=part.val()
    assert shape.isValid(), name+' invalid BREP'
    assert len(shape.Solids())==1, name+' not one connected solid'
    assert shape.Volume()>0
    cq.exporters.export(part,str(OUT/(name+'.stl')),tolerance=.03,angularTolerance=.1)
    cq.exporters.export(part,str(OUT/(name+'.step')))
    bb=shape.BoundingBox()
    report[name]={'valid_solid':True,'solids':1,'volume_mm3':shape.Volume(),'size_mm':[bb.xlen,bb.ylen,bb.zlen]}
assert body.intersect(lid).val().Volume()<1e-6, 'Lid/body interference'
(OUT/'validation.json').write_text(json.dumps(report,indent=2))

def render(ax,part,color):
    verts,faces=part.val().tessellate(.12)
    points=np.array([[v.x,v.y,v.z] for v in verts])
    triangles=points[np.array(faces)]
    ax.add_collection3d(Poly3DCollection(triangles,facecolors=color,shade=True,lightsource=matplotlib.colors.LightSource(azdeg=315,altdeg=45)))
    ax.set_xlim(-12,53); ax.set_ylim(-5,36); ax.set_zlim(-4,28)
    ax.set_box_aspect((65,41,32)); ax.set_axis_off()

fig=plt.figure(figsize=(13,9),facecolor='#f5f7fa')
for panel,title,zshift,direction in [(221,'Exploded • USB side and open cavity',10,(1,-1,1)),(222,'Chest-facing underside • sensor window',0,(1,-1,-1))]:
    compound=cq.Compound.makeCompound([body.val(),lid.translate((0,0,zshift)).val()])
    svg=OUT/f'view_{panel}.svg';png=OUT/f'view_{panel}.png'
    cq.exporters.export(compound,str(svg),opt={'width':700,'height':500,'projectionDir':direction,'showHidden':False,'showAxes':False,'strokeWidth':1.,'strokeColor':(25,91,99)})
    subprocess.run(['inkscape',str(svg),'--export-type=png',f'--export-filename={png}'],check=True,capture_output=True)
    ax=fig.add_subplot(panel);ax.imshow(plt.imread(png));ax.axis('off');ax.set_title(title)
ax=fig.add_subplot(223)
from matplotlib.patches import Rectangle, Circle
ax.add_patch(Rectangle((-2,-1),45,14,facecolor='#d8eeee',edgecolor='#24787b'))
for (x,z,w,h),label in zip((USB_C,USB_CHARGE),('USB-C\nassumed opening','Charge USB\nassumed opening')):
    ax.add_patch(Rectangle((x,z),w,h,facecolor='white',edgecolor='black'));ax.text(x+w/2,z+h+1,label,ha='center',fontsize=9)
ax.set_xlim(-5,46);ax.set_ylim(-4,17);ax.set_aspect('equal');ax.set_xlabel('x (mm), measured from internal left edge');ax.set_ylabel('z (mm)');ax.set_title('USB wall • opening coordinates');ax.grid(alpha=.2)
ax=fig.add_subplot(224)
ax.add_patch(Rectangle((0,0),41,31,facecolor='#d8eeee',edgecolor='#24787b'))
ax.add_patch(Rectangle((29,7),6,3.3,facecolor='white',edgecolor='black'))
ax.annotate('6 × 3.3 mm\nstarts (29, 7)',(32,8.65),(19,20),arrowprops={'arrowstyle':'->'},ha='center')
ax.text(20.5,-3,'USB edge (y = 0)',ha='center');ax.set_xlim(-4,45);ax.set_ylim(35,-6);ax.set_aspect('equal');ax.set_xlabel('x (mm)');ax.set_ylabel('y (mm), inward from USB edge');ax.set_title('Sensor coordinates • cavity reference');ax.grid(alpha=.2)
fig.suptitle('ElderCare case v1 — THIN FIT-CHECK PROTOTYPE',fontsize=20,fontweight='bold')
fig.text(.5,.025,'Internal 41 × 31 × 13 mm | Shell 45 × 35 × 15 mm | With strap loops 59 × 35 × 15 mm\nUSB sizes, button fit and sensor projection require physical checking. Dimensions in mm.',ha='center',fontsize=11)
fig.tight_layout(rect=(0,.07,1,.94));fig.savefig(OUT/'case_preview.png',dpi=170);plt.close(fig)
print(json.dumps(report,indent=2))
