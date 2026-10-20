// Low antialiasing's edge smoothing on the GPU (SDL's gpu renderer, a custom render state), as
// model1::edge_smooth does it on the CPU (model1_edge_aa.cpp, the same sums): drawn over the
// target from the picture so far (the 2D beneath the 3D, the 3D on it: t0), the 3D's own picture
// (t1) telling the 3D's pixels by (KEY's alpha, 1: none there). A pixel by the 3D's edge, with
// contrast enough among it and its four neighbours, goes up to 3/8 of the way towards the
// neighbour across the edge that differs most. Made by tools/gpu/shaders.sh into gpu3d_shaders.h.
Texture2D<float4> u_picture : register(t0, space2);
SamplerState u_sampler : register(s0, space2);
Texture2D<float4> u_3d : register(t1, space2);
SamplerState u_sampler3d : register(s1, space2);

struct PSInput {
	float4 v_color : COLOR0;
	float2 v_uv : TEXCOORD0;
};

int3 rgb(int2 p) { return int3(round(u_picture.Load(int3(p, 0)).rgb * 255.0)); }
bool drawn(int2 p) { return u_3d.Load(int3(p, 0)).a < 0.5; }
int luma(int3 c) { return c.r * 77 + c.g * 150 + c.b * 29; }

float4 main(PSInput input) : SV_Target
{
	uint w, h;
	u_picture.GetDimensions(w, h);
	const int2 p = int2(input.v_uv * float2(w, h));
	const int3 cm = rgb(p);
	const float4 keep = float4(float3(cm) / 255.0, 1.0);
	if (p.x <= 0 || p.y <= 0 || p.x >= int(w) - 1 || p.y >= int(h) - 1) return keep;
	const int2 N = p + int2(0, -1), S = p + int2(0, 1), W = p + int2(-1, 0), E = p + int2(1, 0);
	if (!drawn(p) && !drawn(N) && !drawn(S) && !drawn(W) && !drawn(E)) return keep;
	const int3 cn = rgb(N), cs = rgb(S), cw = rgb(W), ce = rgb(E);
	const int lm = luma(cm), ln = luma(cn), ls = luma(cs), lw = luma(cw), le = luma(ce);
	const int lo = min(min(min(lm, ln), min(ls, lw)), le), hi = max(max(max(lm, ln), max(ls, lw)), le);
	if (hi - lo < 32 * 256) return keep;
	// the edge's direction, and the side across it that differs most
	const bool across_v = abs(ln + ls - 2 * lm) >= abs(lw + le - 2 * lm);
	const int3 c1 = across_v ? cn : cw, c2 = across_v ? cs : ce;
	const int l1 = across_v ? ln : lw, l2 = across_v ? ls : le;
	const int3 other = abs(l1 - lm) >= abs(l2 - lm) ? c1 : c2;
	// how much the pixel stands out from its neighbours' average (FXAA's subpixel amount)
	const float sub = min(1.0, abs(float(ln + ls + lw + le) / 4.0 - float(lm)) / float(hi - lo));
	const float s = sub * sub * (3.0 - 2.0 * sub);
	const int k = int(round(s * s * 0.75 * 0.5 * 256.0));
	if (k == 0) return keep;
	const int3 mixed = cm + (((other - cm) * k + 128) >> 8);
	return float4(float3(mixed) / 255.0, 1.0);
}
