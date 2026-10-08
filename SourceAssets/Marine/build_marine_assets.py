"""Editable 38 m yacht source and seamless, deterministic marine surface maps."""
import bpy, bmesh, math, json
import numpy as np
from pathlib import Path
from mathutils import Vector

OUT = Path(__file__).resolve().parent / 'marine-assets'
OUT.mkdir(exist_ok=True)
bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.delete(use_global=False)
parts = []

def material(name, colour, rough=.4, metal=0):
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    bsdf = mat.node_tree.nodes.get('Principled BSDF')
    bsdf.inputs['Base Color'].default_value = (*colour,1)
    bsdf.inputs['Roughness'].default_value = rough
    bsdf.inputs['Metallic'].default_value = metal
    return mat

white = material('M_NC2_Paint_White',(.85,.88,.88),.25)
teak = material('M_NC2_Wood_Deck',(.48,.29,.12),.62)
glass = material('M_NC2_Glass_Blue',(.018,.055,.085),.12,.15)
chrome = material('M_NC2_Chrome',(.65,.7,.74),.22,.95)
fabric = material('M_NC2_Canvas_White',(.78,.73,.61),.85)
navy = material('M_NC2_Hull_Blue',(.018,.035,.055),.32)

def finish(obj, mat, smooth=False):
    obj.data.materials.append(mat)
    if smooth:
        for face in obj.data.polygons: face.use_smooth = True
    parts.append(obj)
    return obj

def box(name, loc, size, mat, bevel=.04):
    bpy.ops.mesh.primitive_cube_add(size=1, location=loc)
    obj = bpy.context.object
    obj.name = name
    obj.dimensions = size
    bpy.ops.object.transform_apply(location=False,rotation=False,scale=True)
    finish(obj,mat)
    if bevel:
        mod = obj.modifiers.new('Rounded manufactured edges','BEVEL')
        mod.width = bevel
        mod.segments = 3
        bpy.ops.object.modifier_apply(modifier=mod.name)
    return obj

def tube(name, start, end, radius, mat=chrome):
    a,b = Vector(start),Vector(end)
    bpy.ops.mesh.primitive_cylinder_add(vertices=8, radius=radius, depth=(b-a).length, location=(a+b)/2)
    obj=bpy.context.object
    obj.name=name
    obj.rotation_euler=(b-a).to_track_quat('Z','Y').to_euler()
    return finish(obj,mat,True)

# Closed displacement hull: bow faces -Y; keel origin and original dimensions retained.
stations=[(-19,.045),(-18,.8),(-16,2.15),(-13,3.05),(-9,3.55),(-5,3.8),
          (0,3.8),(5,3.75),(10,3.6),(15,3.38),(18,3.2)]
profile=[(0,0),(.45,.24),(.82,1.05),(.96,2.2),(1,3.2),(-1,3.2),(-.96,2.2),(-.82,1.05),(-.45,.24)]
verts=[(w*x,y,z) for y,w in stations for x,z in profile]
faces=[]
n=len(profile)
for row in range(len(stations)-1):
    for k in range(n):
        faces.append((row*n+k,row*n+(k+1)%n,(row+1)*n+(k+1)%n,(row+1)*n+k))
faces += [tuple(reversed(range(n))),tuple((len(stations)-1)*n+k for k in range(n))]
mesh=bpy.data.meshes.new('Closed shaped hull')
mesh.from_pydata(verts,[],faces)
mesh.update()
obj=bpy.data.objects.new('Superyacht hull',mesh)
bpy.context.collection.objects.link(obj)
finish(obj,white,True)

# Deck slabs, recessed dark window bands and rounded white superstructure.
box('Main teak promenade',(0,2.2,3.23),(6.8,28.5,.08),teak,.1)
box('Lower saloon',(0,1.1,4.28),(5.45,18.2,2.05),white,.22)
box('Lower panoramic glass',(0,1.05,4.55),(5.51,17.6,1.04),glass,.19)
box('Lower roof',(0,1.15,5.42),(6.55,20.6,.24),white,.16)
box('Upper aft teak deck',(0,8.25,5.58),(6.0,5.8,.075),teak,.1)
box('Bridge house',(0,-2.1,6.36),(4.5,9.8,1.65),white,.2)
box('Bridge glazing',(0,-2.3,6.57),(4.56,9.3,.92),glass,.18)
box('Flybridge roof',(0,-1.5,7.32),(5.65,11.6,.22),white,.15)
box('Flybridge teak',(0,-1.25,7.46),(5.2,10.8,.07),teak,.1)
for side in (-1,1):
    for y in (-6,-2,2,6):
        box('Saloon window mullion',(side*2.775,y,4.56),(.055,.095,1.11),chrome,.012)
    for y in (-5.5,-2.4,1):
        box('Bridge mullion',(side*2.305,y,6.59),(.055,.08,1),chrome,.01)
    for y in (-10,-7,-4,0,4,8,12):
        box('Hull porthole',(side*3.48,y,2.44),(.06,.85,.32),glass,.11)
    # Broad rubbing strip, deliberately inset from the hull bounds.
    tube('Gunwale trim',(side*3.47,-10,3.05),(side*3.47,14,3.05),.045,navy)

def railing(points, deck_z):
    for x,y in points:
        tube('Rail stanchion',(x,y,deck_z),(x,y,deck_z+.9),.025)
    for a,b in zip(points,points[1:]):
        for height in (.45,.9):
            tube('Safety rail',(a[0],a[1],deck_z+height),(b[0],b[1],deck_z+height),.021)

for side in (-1,1):
    points=[(side*min(w-.12,3.62),y) for y,w in stations if y>=-18]
    railing(points,3.27)
    railing([(side*3.0,y) for y in (5.5,7.5,9.5,11)],5.62)
    railing([(side*2.52,y) for y in (-6,-4,-2,0,2,4)],7.5)
railing([(-3.05,17.7),(0,17.7),(3.05,17.7)],3.27)
railing([(-3,11),(0,11),(3,11)],5.62)
railing([(-2.52,4),(0,4),(2.52,4)],7.5)

# Transom beach platform and teak stairs; no overall length enlargement.
box('Swim platform',(0,18.3,1.18),(6.05,1.35,.26),white,.14)
box('Swim platform teak',(0,18.3,1.33),(5.8,1.21,.07),teak,.06)
for step in range(6):
    box('Transom stair',(-2.35,17.2-step*.27,1.53+step*.29),(1.05,.38,.17),teak,.03)
for step in range(9):
    box('Upper deck stair',(2.1,8.7-step*.3,3.45+step*.235),(1.02,.4,.16),teak,.025)
    tube('Stair baluster',(2.63,8.7-step*.3,3.5+step*.235),(2.63,8.7-step*.3,4.25+step*.235),.02)
tube('Stair handrail',(2.63,8.7,4.25),(2.63,6.3,6.13),.025)

def sofa(x,y,z,length=2.1):
    box('Lounge base',(x,y,z),(.88,length,.33),white,.1)
    box('Cream seat cushion',(x,y,z+.24),(.78,length-.1,.18),fabric,.08)
    box('Lounge back',(x+.34,y,z+.59),(.15,length,.53),fabric,.08)
for x in (-1.85,1.5):
    sofa(x,12.7,3.63,2.9)
    sofa(x,9.3,5.93,2.5)
box('Aft dining tabletop',(0,13.1,4.16),(1.6,2.4,.11),teak,.12)
tube('Table pedestal',(0,13.1,3.27),(0,13.1,4.1),.1)
for x in (-1,1):
    box('Foredeck sun mattress',(x,-11.7,3.56),(1.65,2.65,.24),fabric,.13)
    box('Flybridge lounger',(x,1.5,7.69),(1.55,2.2,.24),fabric,.1)
box('Flybridge console',(0,-4.85,7.83),(1.8,.65,.65),white,.09)
box('Navigation screen',(0,-4.49,8.07),(1.2,.025,.33),glass,.035)
for x in (-.52,.52):
    box('Helm seat',(x,-3.55,7.87),(.58,.63,.28),fabric,.08)
    box('Helm seat back',(x,-3.22,8.12),(.58,.13,.65),fabric,.06)
# Radar arch and compact satellite domes kept below original 8.5 m height.
for x in (-2,2):
    tube('Radar arch leg',(x,-.7,7.5),(x,-.7,8.15),.075,white)
tube('Radar arch beam',(-2,-.7,8.15),(2,-.7,8.15),.085,white)
for x in (-1.4,1.4):
    bpy.ops.mesh.primitive_uv_sphere_add(segments=16,ring_count=8,radius=.26,location=(x,-.7,8.35))
    finish(bpy.context.object,white,True)

# Consolidate into one mesh, retain material slots and explicit UVs for movable decks.
bpy.ops.object.select_all(action='DESELECT')
for obj in parts: obj.select_set(True)
bpy.context.view_layer.objects.active=parts[0]
bpy.ops.object.join()
yacht=bpy.context.object
yacht.name='SM_NC2_Yacht'
bpy.ops.object.transform_apply(location=True,rotation=True,scale=True)
bpy.context.scene.cursor.location=(0,0,0)
bpy.ops.object.origin_set(type='ORIGIN_CURSOR')
bm=bmesh.new()
bm.from_mesh(yacht.data)
bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces))
bm.to_mesh(yacht.data)
bm.free()
if not yacht.data.uv_layers: yacht.data.uv_layers.new(name='MarineUV')
uv=yacht.data.uv_layers.active.data
for face in yacht.data.polygons:
    normal=face.normal
    dominant=max(range(3),key=lambda axis:abs(normal[axis]))
    axes=(0,1) if dominant==2 else ((1,2) if dominant==0 else (0,2))
    for index in face.loop_indices:
        vertex=yacht.data.vertices[yacht.data.loops[index].vertex_index].co
        uv[index].uv=(vertex[axes[0]]/2,vertex[axes[1]]/2)
tri=yacht.modifiers.new('Export triangles','TRIANGULATE')
bpy.ops.object.modifier_apply(modifier=tri.name)
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'Superyacht38m.blend'))
bpy.ops.export_scene.fbx(filepath=str(OUT/'SM_NC2_Yacht.fbx'),use_selection=True,
    object_types={'MESH'},apply_unit_scale=True,axis_forward='-Z',axis_up='Y',
    add_leaf_bones=False,bake_anim=False,path_mode='AUTO',mesh_smooth_type='FACE')
OUT.joinpath('yacht_geometry.json').write_text(json.dumps({'triangles':len(yacht.data.polygons),'vertices':len(yacht.data.vertices),'length_metres':38,'width_metres':7.6,'materials':[m.name for m in yacht.data.materials]},indent=2))

# Seamless coral microstructure and teak seams: generated source maps, not external images.
size=512
yy,xx=np.mgrid[0:size,0:size]/size
def texture(name,rgb):
    img=bpy.data.images.new(name,width=size,height=size,alpha=True)
    pixels=np.ones((size,size,4),np.float32)
    pixels[:,:,:3]=np.clip(rgb,0,1)
    img.pixels.foreach_set(pixels.ravel())
    img.filepath_raw=str(OUT/(name+'.png'))
    img.file_format='PNG'
    img.save()
    bpy.data.images.remove(img)
pores=(np.sin(xx*math.tau*37)*np.sin(yy*math.tau*43))**6
grain=.5+.18*np.sin(xx*math.tau*13+np.sin(yy*math.tau*11))+.1*np.sin(yy*math.tau*61)
height=grain-.27*pores
texture('Coral_Albedo',np.repeat((.55+.32*height)[:,:,None],3,axis=2))
dx=(np.roll(height,-1,axis=1)-np.roll(height,1,axis=1))*3
dy=(np.roll(height,-1,axis=0)-np.roll(height,1,axis=0))*3
normal=np.stack((-dx,-dy,np.ones_like(dx)),axis=2)
normal/=np.linalg.norm(normal,axis=2)[:,:,None]
texture('Coral_Normal',normal*.5+.5)
seam=(np.mod(xx*8,1)<.025)
woodgrain=.8+.1*np.sin(xx*math.tau*97+np.sin(yy*math.tau*7)*2)+.045*np.sin(yy*math.tau*57)
woodgrain=np.where(seam,.16,woodgrain)
texture('Teak_Albedo',woodgrain[:,:,None]*np.array([.58,.37,.19]))
texture('Teak_Normal',np.stack((np.zeros_like(xx)+.5,np.zeros_like(xx)+.5,np.ones_like(xx)),axis=2))

# Asset-only presentation render; no game/PIE run.
scene=bpy.context.scene
scene.render.engine='BLENDER_EEVEE'
scene.render.resolution_x=1200
scene.render.resolution_y=800
scene.render.resolution_percentage=100
scene.world.color=(.2,.2,.2)
bpy.ops.object.light_add(type='AREA',location=(12,-14,24))
bpy.context.object.data.energy=6000
bpy.context.object.data.size=20
bpy.ops.object.light_add(type='AREA',location=(-12,10,15))
bpy.context.object.data.energy=3500
bpy.context.object.data.size=15
bpy.ops.object.camera_add(location=(37,-43,28))
camera=bpy.context.object
camera.rotation_euler=(Vector((0,0,3.4))-camera.location).to_track_quat('-Z','Y').to_euler()
camera.data.type='ORTHO'
camera.data.ortho_scale=47
camera.data.clip_end=1000
scene.camera=camera
scene.render.filepath=str(OUT/'Superyacht38m.png')
bpy.ops.render.render(write_still=True)
