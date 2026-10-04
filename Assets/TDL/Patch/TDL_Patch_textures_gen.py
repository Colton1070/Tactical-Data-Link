"""Textures for TDL_Patch (unfolded layout).

Slot 1 (face + rim, one texture, 512x512): the face sits inset by one rim thickness on every
side (u 0.032, v 0.053) and each rim strip continues outward from its face edge into that
margin, so the texture reads as the patch unfolded flat. Face: weave, folded lip, two inset
rows of running stitch. Margin: the fold rolling over the edge.
  TDL_Patch_Face_NMO.tif  — R +X, G -Y (DirectX), B metalness, A AO
  TDL_Patch_Face_BCR.tif  — greyscale so the material Color tints it; A = roughness
Slot 2 (back, 256x256): hook velcro.
  TDL_Patch_Back_NMO.tif / TDL_Patch_Back_BCR.tif
8-bit RGBA TIFF, LZW.
"""
import numpy as np
from PIL import Image

W_MM, H_MM, T_MM = 88.9, 50.8, 3.0
FOLD_MM = 1.2
STITCH_ROWS_MM = [3.0]
STITCH_LEN_MM, STITCH_GAP_MM, STITCH_W_MM = 2.2, 1.0, 0.55
MU = T_MM / (W_MM + 2*T_MM)
MV = T_MM / (H_MM + 2*T_MM)

def fbm(shape, octaves=4, base=8, seed=0):
    r = np.random.default_rng(seed)
    out = np.zeros(shape, np.float32); amp = 1.0; freq = base; tot = 0.0
    for _ in range(octaves):
        small = r.random((max(2, shape[0]*freq//shape[0]+1), max(2, shape[1]*freq//shape[1]+1))).astype(np.float32)
        im = Image.fromarray((small*255).astype(np.uint8)).resize((shape[1], shape[0]), Image.BICUBIC)
        out += amp * (np.asarray(im, np.float32)/255.0); tot += amp
        amp *= 0.5; freq *= 2
    return out/tot

def height_to_normal(h, strength):
    dhdx = np.zeros_like(h); dhdy = np.zeros_like(h)
    dhdx[:, 1:-1] = (h[:, 2:] - h[:, :-2]) * 0.5
    dhdy[1:-1, :] = (h[:-2, :] - h[2:, :]) * 0.5
    nx = -dhdx * strength; ny = -dhdy * strength; nz = np.ones_like(h)
    l = np.sqrt(nx*nx + ny*ny + nz*nz)
    return nx/l, ny/l

def pack_nmo(nx, ny, metal, ao):
    return np.dstack([nx*0.5 + 0.5, (-ny)*0.5 + 0.5, metal, ao])

def to_u8(img): return (np.clip(img, 0, 1)*255 + 0.5).astype(np.uint8)
def save_tif(arr, path): Image.fromarray(to_u8(arr), "RGBA").save(path, compression="tiff_lzw")
def save_png(arr, path): Image.fromarray(to_u8(arr), "RGBA").save(path)

# ------------------------------------------------------------------ face + rim (unfolded)
N = 512
u = (np.arange(N) + 0.5) / N
v = 1.0 - (np.arange(N) + 0.5) / N
U, V = np.meshgrid(u, v)
# physical coords on the unfolded patch: face is [0,W]x[0,H]; the margin runs 0..T beyond it
x_mm = (U - MU) / (1 - 2*MU) * W_MM
y_mm = (V - MV) / (1 - 2*MV) * H_MM
in_face = (x_mm >= 0) & (x_mm <= W_MM) & (y_mm >= 0) & (y_mm <= H_MM)
dx_out = np.maximum(np.maximum(-x_mm, x_mm - W_MM), 0)
dy_out = np.maximum(np.maximum(-y_mm, y_mm - H_MM), 0)
out_mm = np.sqrt(dx_out**2 + dy_out**2)
d_edge = np.where(in_face, np.minimum(np.minimum(x_mm, W_MM - x_mm), np.minimum(y_mm, H_MM - y_mm)), 0.0)

weave = 0.5 + 0.5*np.clip(0.5*np.sin(2*np.pi*(x_mm + y_mm)/0.7) + 0.5*np.sin(2*np.pi*(x_mm - y_mm)/0.7), -1, 1)
weave = 0.55*weave + 0.45*fbm((N, N), 5, 16, seed=3)
field_h = weave * 0.10

# folded edge: a soft roll that starts FOLD_MM inside the face and keeps rolling across the
# margin (the rim strip) so the fold is continuous over the mesh edge
t_fold = np.clip(d_edge / FOLD_MM, 0, 1)
lip_in = -(1.0 - t_fold)**2 * 0.35
roll_out = -(0.35 + 0.9*np.clip(out_mm / T_MM, 0, 1)**1.5)
lip = np.where(in_face, lip_in, roll_out)
edge_fuzz = np.where((d_edge < FOLD_MM*1.5) | ~in_face, fbm((N, N), 3, 64, seed=11)*0.04, 0.0)

# one inset row of running stitch
near_x = np.minimum(x_mm, W_MM - x_mm) < np.minimum(y_mm, H_MM - y_mm)
along = np.where(near_x, y_mm, x_mm)
stitch_h = np.zeros((N, N), np.float32); stitch_mask = np.zeros((N, N), np.float32); groove = np.zeros((N, N), np.float32)
pitch = STITCH_LEN_MM + STITCH_GAP_MM
for i, inset in enumerate(STITCH_ROWS_MM):
    band = np.exp(-((d_edge - inset) / (STITCH_W_MM*0.6))**2) * in_face
    phase = (along + i*pitch*0.5) % pitch
    on = np.clip(1 - np.abs(phase - STITCH_LEN_MM/2) / (STITCH_LEN_MM/2), 0, 1)**0.5
    crown = band*on
    stitch_h += crown*0.40
    stitch_mask = np.maximum(stitch_mask, crown)
    groove += np.exp(-((d_edge - inset) / (STITCH_W_MM*1.6))**2) * in_face * 0.12

h_face = field_h + lip + edge_fuzz + stitch_h - groove
nx, ny = height_to_normal(h_face, strength=6.0)
ao = 1.0 - groove*1.2 - (1.0 - t_fold)*0.18*in_face - np.clip(out_mm / T_MM, 0, 1)*0.30
ao = np.clip(ao, 0.5, 1.0)
metal = np.zeros((N, N), np.float32)
face_nmo = pack_nmo(nx, ny, metal, ao)
save_tif(face_nmo, "/mnt/user-data/outputs/TDL_Patch_Face_NMO.tif")

g = 0.62 * (0.94 + 0.12*weave)
g = g + stitch_mask*0.16 - (1.0 - t_fold)*0.08*in_face - np.clip(out_mm / T_MM, 0, 1)*0.10
g = g * (0.95 + 0.10*fbm((N, N), 3, 24, seed=41))
alb = np.repeat(np.clip(g, 0, 1)[..., None], 3, axis=2).astype(np.float32)
rough = np.clip(0.88 - stitch_mask*0.08, 0, 1).astype(np.float32)
face_bcr = np.dstack([alb, rough])
save_tif(face_bcr, "/mnt/user-data/outputs/TDL_Patch_Face_BCR.tif")
save_png(face_bcr, "/mnt/user-data/outputs/TDL_Patch_Face_BCR_preview.png")

# ------------------------------------------------------------------ back (velcro)
M = 256
u = (np.arange(M) + 0.5) / M
v = 1.0 - (np.arange(M) + 0.5) / M
U, V = np.meshgrid(u, v)
hook_pitch = 1.1
bx = U * W_MM; by = V * H_MM
jx = fbm((M, M), 2, 32, seed=21)*0.4; jy = fbm((M, M), 2, 32, seed=22)*0.4
cx = (bx/hook_pitch + jx) % 1.0 - 0.5; cy = (by/hook_pitch + jy) % 1.0 - 0.5
hook = np.clip(1 - np.sqrt(cx*cx + cy*cy)/0.42, 0, 1)**0.8
h_back = hook*0.6 + fbm((M, M), 4, 16, seed=9)*0.15
nx, ny = height_to_normal(h_back, strength=5.0)
ao = np.clip(0.55 + hook*0.4, 0.4, 1)
back_nmo = pack_nmo(nx, ny, np.zeros((M, M), np.float32), ao)
save_tif(back_nmo, "/mnt/user-data/outputs/TDL_Patch_Back_NMO.tif")
velcro = 0.07 * (0.7 + 0.8*hook)
alb = np.repeat(np.clip(velcro, 0, 1)[..., None], 3, axis=2).astype(np.float32)
back_bcr = np.dstack([alb, np.full((M, M), 0.9, np.float32)])
save_tif(back_bcr, "/mnt/user-data/outputs/TDL_Patch_Back_BCR.tif")

def rgb(a): return to_u8(a[..., :3])
sheet = Image.new("RGB", (1024, 512), (40, 40, 40))
sheet.paste(Image.fromarray(rgb(face_nmo)), (0, 0))
sheet.paste(Image.fromarray(rgb(face_bcr)), (512, 0))
sheet.save("/home/claude/patch_sheet.png")
print("done")
