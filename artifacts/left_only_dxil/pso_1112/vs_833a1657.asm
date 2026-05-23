;
; Input signature:
;
; Name                 Index   Mask Register SysValue  Format   Used
; -------------------- ----- ------ -------- -------- ------- ------
; ATTRIBUTE                0   xyzw        0     NONE   float   xyz 
; ATTRIBUTE               13   x           1     NONE    uint   x   
; SV_InstanceID            0   x           2   INSTID    uint   x   
; SV_VertexID              0   x           3   VERTID    uint   x   
;
;
; Output signature:
;
; Name                 Index   Mask Register SysValue  Format   Used
; -------------------- ----- ------ -------- -------- ------- ------
; TEXCOORD10_centroid      0   xyzw        0     NONE   float   xyzw
; TEXCOORD11_centroid      0   xyzw        1     NONE   float   xyzw
; PRIMITIVE_ID             0   x           2     NONE    uint   x   
; TEXCOORD                 7   xyzw        3     NONE   float   xyzw
; TEXCOORD                 9   xyz         4     NONE   float   xyz 
; SV_Position              0   xyzw        5      POS   float   xyzw
;
; shader debug name: ce08df19ff244c85d6ff5f5bbb13288e.pdb
; shader hash: ce08df19ff244c85d6ff5f5bbb13288e
;
; Pipeline Runtime Information: 
;
;PSVRuntimeInfo:
; Vertex Shader
; OutputPositionPresent=1
; MinimumExpectedWaveLaneCount: 0
; MaximumExpectedWaveLaneCount: 4294967295
; UsesViewID: false
; SigInputElements: 4
; SigOutputElements: 6
; SigPatchConstOrPrimElements: 0
; SigInputVectors: 4
; SigOutputVectors[0]: 6
; SigOutputVectors[1]: 0
; SigOutputVectors[2]: 0
; SigOutputVectors[3]: 0
; EntryFunctionName: Main
;
;
; Input signature:
;
; Name                 Index             InterpMode DynIdx
; -------------------- ----- ---------------------- ------
; ATTRIBUTE                0                              
; ATTRIBUTE               13                              
; SV_InstanceID            0                              
; SV_VertexID              0                              
;
; Output signature:
;
; Name                 Index             InterpMode DynIdx
; -------------------- ----- ---------------------- ------
; TEXCOORD10_centroid      0                 linear       
; TEXCOORD11_centroid      0                 linear       
; PRIMITIVE_ID             0        nointerpolation       
; TEXCOORD                 7                 linear       
; TEXCOORD                 9                 linear       
; SV_Position              0          noperspective       
;
; Buffer Definitions:
;
; cbuffer 
; {
;
;   [3764 x i8] (type annotation not present)
;
; }
;
; cbuffer 
; {
;
;   [10076 x i8] (type annotation not present)
;
; }
;
; cbuffer 
; {
;
;   [348 x i8] (type annotation not present)
;
; }
;
; cbuffer 
; {
;
;   [60 x i8] (type annotation not present)
;
; }
;
; Resource bind info for 
; {
;
;   [16 x i8] (type annotation not present)
;
; }
;
; Resource bind info for 
; {
;
;   [4 x i8] (type annotation not present)
;
; }
;
; Resource bind info for 
; {
;
;   [16 x i8] (type annotation not present)
;
; }
;
; Resource bind info for 
; {
;
;   [16 x i8] (type annotation not present)
;
; }
;
;
; Resource Bindings:
;
; Name                                 Type  Format         Dim      ID      HLSL Bind  Count
; ------------------------------ ---------- ------- ----------- ------- -------------- ------
;                                   cbuffer      NA          NA     CB0            cb0     1
;                                   cbuffer      NA          NA     CB1            cb1     1
;                                   cbuffer      NA          NA     CB2            cb2     1
;                                   cbuffer      NA          NA     CB3            cb3     1
;                                   sampler      NA          NA      S0             s0     1
;                                   sampler      NA          NA      S1             s1     1
;                                   sampler      NA          NA      S2             s2     1
;                                   sampler      NA          NA      S3             s3     1
;                                   texture     f32        cube      T0             t0     1
;                                   texture     f32          2d      T1             t1     1
;                                   texture     f32          2d      T2             t2     1
;                                   texture     f32          3d      T3             t3     1
;                                   texture  struct         r/o      T4             t4     1
;                                   texture  struct         r/o      T5             t5     1
;                                   texture  struct         r/o      T6             t6     1
;                                   texture  struct         r/o      T7             t7     1
;                                   texture     f32         buf      T8             t8     1
;
;
; ViewId state:
;
; Number of inputs: 13, outputs: 24
; Outputs dependent on ViewId: {  }
; Inputs contributing to computation of Outputs:
;   output 0 depends on inputs: { 4, 8, 12 }
;   output 1 depends on inputs: { 4, 8, 12 }
;   output 2 depends on inputs: { 4, 8, 12 }
;   output 4 depends on inputs: { 4, 8, 12 }
;   output 5 depends on inputs: { 4, 8, 12 }
;   output 6 depends on inputs: { 4, 8, 12 }
;   output 7 depends on inputs: { 4, 8, 12 }
;   output 8 depends on inputs: { 4, 8 }
;   output 12 depends on inputs: { 0, 1, 2, 4, 8 }
;   output 13 depends on inputs: { 0, 1, 2, 4, 8 }
;   output 14 depends on inputs: { 0, 1, 2, 4, 8 }
;   output 15 depends on inputs: { 0, 1, 2, 4, 8 }
;   output 16 depends on inputs: { 0, 1, 2, 4, 8 }
;   output 17 depends on inputs: { 0, 1, 2, 4, 8 }
;   output 18 depends on inputs: { 0, 1, 2, 4, 8 }
;   output 20 depends on inputs: { 0, 1, 2, 4, 8 }
;   output 21 depends on inputs: { 0, 1, 2, 4, 8 }
;   output 22 depends on inputs: { 0, 1, 2, 4, 8 }
;   output 23 depends on inputs: { 0, 1, 2, 4, 8 }
;
target datalayout = "e-m:e-p:32:32-i1:32-i8:32-i16:32-i32:32-i64:64-f16:32-f32:32-f64:64-n8:16:32:64"
target triple = "dxil-ms-dx"

%dx.types.Handle = type { i8* }
%dx.types.ResBind = type { i32, i32, i32, i8 }
%dx.types.ResourceProperties = type { i32, i32 }
%dx.types.CBufRet.f32 = type { float, float, float, float }
%dx.types.CBufRet.i32 = type { i32, i32, i32, i32 }
%dx.types.ResRet.f32 = type { float, float, float, float, i32 }
%dx.types.ResRet.i32 = type { i32, i32, i32, i32, i32 }
%"class.TextureCube<vector<float, 4> >" = type { <4 x float> }
%"class.Texture2D<vector<float, 4> >" = type { <4 x float>, %"class.Texture2D<vector<float, 4> >::mips_type" }
%"class.Texture2D<vector<float, 4> >::mips_type" = type { i32 }
%"class.Texture3D<vector<float, 4> >" = type { <4 x float>, %"class.Texture3D<vector<float, 4> >::mips_type" }
%"class.Texture3D<vector<float, 4> >::mips_type" = type { i32 }
%"class.StructuredBuffer<vector<float, 4> >" = type { <4 x float> }
%"class.StructuredBuffer<unsigned int>" = type { i32 }
%"class.Buffer<vector<float, 4> >" = type { <4 x float> }
%hostlayout.TranslucentBasePass = type { i32, i32, i32, i32, i32, i32, i32, i32, <3 x i32>, i32, i32, i32, i32, i32, <3 x float>, float, <3 x float>, float, float, float, float, float, <3 x float>, float, float, float, i32, i32, <2 x float>, i32, i32, <4 x float>, [4 x [4 x <4 x float>]], [4 x <4 x float>], <4 x float>, float, i32, i32, i32, <4 x float>, [4 x <4 x float>], i32, i32, i32, i32, <4 x float>, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, float, <4 x float>, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, float, <4 x float>, <4 x float>, <4 x float>, <4 x float>, [3 x <4 x float>], <3 x float>, float, <2 x float>, float, float, [2 x [4 x <4 x float>]], [2 x <4 x float>], <2 x float>, i32, i32, i32, i32, i32, float, <4 x float>, <4 x float>, <4 x float>, <4 x float>, <4 x float>, <4 x float>, <4 x float>, <2 x float>, float, float, <3 x float>, float, float, float, float, float, <3 x float>, float, <4 x float>, <4 x float>, <4 x float>, <4 x float>, <4 x float>, <4 x float>, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, float, <4 x float>, <4 x float>, <4 x float>, <4 x float>, <4 x float>, <4 x float>, <4 x float>, <2 x float>, float, float, <3 x float>, float, float, float, float, float, <3 x float>, float, <4 x float>, <4 x float>, <4 x float>, <4 x float>, <4 x float>, <4 x float>, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, float, i32, i32, <2 x i32>, i32, i32, float, i32, float, float, float, float, <4 x float>, <3 x float>, float, <3 x float>, float, i32, float, float, float, i32, i32, i32, i32, i32, i32, float, float, i32, float, float, float, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, float, float, float, i32, i32, i32, i32, i32, i32, float, float, i32, i32, i32, i32, i32, float, float, float, [4 x <4 x float>], float, float, i32, i32, i32, i32, float, float, i32, i32, i32, i32, float, float, i32, i32, i32, float, float, float, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, <2 x float>, <2 x float>, float, float, <4 x i32>, <2 x float>, float, float, <4 x float>, <4 x float>, <2 x float>, <2 x float>, i32, i32, i32, i32, <4 x float>, <2 x float>, float, float, <4 x float>, <2 x float>, <2 x float>, float, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, float, float, <2 x float>, <2 x float>, i32, i32, i32, i32, i32, i32, i32, i32, float, float, float, float, float, float, float, float, float, float, <2 x i32>, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, float, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, [6 x <4 x float>], [6 x <4 x float>], <2 x float>, <2 x float>, <2 x float>, float, i32, i32, i32, i32, float, i32, i32, i32, i32, i32, i32, i32, float, float, float, float, float, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, <3 x float>, i32, <3 x i32>, float, i32, i32, i32, i32, i32, i32, i32, i32, i32, float, float, float, <3 x i32>, i32, <3 x i32>, i32, i32, i32, i32, i32, i32, float, float, float, [6 x [4 x <4 x float>]], <3 x float>, float, <4 x float>, <2 x i32>, i32, i32, i32, i32, float, float, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32 }
%hostlayout.View = type { [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <4 x float>, <4 x float>, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], <4 x float>, <4 x float>, <2 x float>, <2 x float>, <2 x float>, <2 x float>, <4 x float>, <4 x float>, <4 x i32>, <4 x float>, <4 x float>, <4 x float>, <4 x float>, <2 x float>, <2 x float>, i32, float, float, float, <4 x float>, <4 x float>, <4 x float>, <2 x float>, <2 x float>, float, float, float, float, <3 x float>, float, float, float, float, float, float, float, float, i32, i32, i32, i32, i32, i32, i32, i32, float, float, float, float, float, <4 x float>, <3 x float>, float, [2 x <4 x float>], [2 x <4 x float>], <4 x float>, <4 x float>, float, float, float, float, float, float, float, float, float, float, float, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, [2 x <4 x float>], [2 x <4 x float>], [2 x <4 x float>], [2 x <4 x float>], [2 x <4 x float>], <4 x float>, <3 x float>, float, <4 x float>, [4 x <4 x float>], <4 x float>, float, float, float, float, <4 x float>, float, float, float, float, float, float, float, float, <3 x float>, float, float, float, float, float, <4 x float>, float, float, float, float, <4 x float>, float, float, float, float, [8 x <4 x float>], float, float, float, float, i32, float, float, float, <3 x float>, i32, [6 x <4 x float>], [6 x <4 x float>], [6 x <4 x float>], [6 x <4 x float>], float, float, i32, i32, <3 x float>, float, <3 x float>, float, float, float, i32, float, float, float, float, float, float, float, <2 x i32>, float, float, float, float, <3 x float>, float, <3 x float>, float, <2 x float>, <2 x float>, <2 x float>, <2 x float>, <2 x float>, float, float, <3 x float>, float, <2 x float>, <2 x float>, float, float, float, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, float, float, float, float, [2 x <4 x float>], <4 x float>, i32, i32, i32, i32, i32, i32, i32, float, <4 x float>, i32, i32, i32, i32, <4 x i32>, <2 x float>, float, float, <4 x float>, <4 x float>, <4 x float>, <3 x float>, float, i32, i32, i32, i32, [32 x <4 x i32>], i32, float, float, float, <4 x float>, <4 x float>, <4 x float>, <4 x float>, <2 x float>, float, float, <4 x float>, <4 x float>, <4 x float>, float, float, float, i32, <4 x i32>, i32, i32, i32, i32, [4 x <4 x float>], <3 x i32>, i32, <3 x float>, float, <3 x float>, float, <3 x i32>, i32, [16 x <4 x float>], float, float, float, float, float, float, float, float, [4 x [4 x <4 x float>]], [4 x <4 x float>], [4 x <4 x float>], [16 x [4 x <4 x float>]], [16 x <4 x float>], [16 x <4 x float>], [8 x [4 x <4 x float>]], [8 x <4 x float>], [8 x <4 x float>], [8 x <4 x float>], [8 x <4 x float>], [2 x <4 x float>], [2 x <4 x float>], [2 x <4 x float>], [2 x <4 x float>], [2 x <4 x float>], [2 x <4 x float>], [2 x <4 x float>], [2 x <4 x float>], [2 x <4 x float>], [2 x <4 x float>], <4 x float>, <4 x float>, float, i32, i32, float, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, <3 x float>, i32, <4 x float>, <4 x float>, <4 x float>, <3 x float>, i32, <3 x i32>, i32, <3 x i32>, i32, <3 x i32>, i32, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, <4 x float>, float, float, i32, i32, i32, i32, i32, i32, <4 x float>, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, <3 x i32>, i32, <3 x i32>, i32, <3 x float>, float, <3 x float> }
%Scene = type { i32, i32, i32, i32, i32, i32, i32, float, i32, i32, i32, i32, i32, i32, i32, i32, i32, float, float, float, i32, i32, i32, i32, i32, i32, i32, i32, <4 x i32>, i32, i32, i32, i32, <4 x i32>, i32, i32, i32, i32, i32, float, float, float, i32, i32, i32, i32, i32, i32, i32, float, i32, i32, i32, i32, i32, i32, i32, float, <2 x float>, i32, i32, i32, i32, i32, float, i32, i32, i32, i32, i32, float, float, float, i32, i32, i32, float, i32, i32, i32 }
%LocalVF = type { <4 x i32>, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32 }
%struct.SamplerState = type { i32 }

define void @Main() {
  %1 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 8, i32 8, i32 0, i8 0 }, i32 8, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %2 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 7, i32 7, i32 0, i8 0 }, i32 7, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %3 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 6, i32 6, i32 0, i8 0 }, i32 6, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %4 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 5, i32 5, i32 0, i8 0 }, i32 5, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %5 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 4, i32 4, i32 0, i8 0 }, i32 4, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %6 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 3, i32 3, i32 0, i8 0 }, i32 3, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %7 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 2, i32 2, i32 0, i8 0 }, i32 2, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %8 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 1, i32 1, i32 0, i8 0 }, i32 1, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %9 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind zeroinitializer, i32 0, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %10 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 3, i32 3, i32 0, i8 3 }, i32 3, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %11 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 2, i32 2, i32 0, i8 3 }, i32 2, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %12 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 1, i32 1, i32 0, i8 3 }, i32 1, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %13 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 0, i32 0, i32 0, i8 3 }, i32 0, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %14 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 3, i32 3, i32 0, i8 2 }, i32 3, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %15 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 2, i32 2, i32 0, i8 2 }, i32 2, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %16 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 1, i32 1, i32 0, i8 2 }, i32 1, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %17 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 0, i32 0, i32 0, i8 2 }, i32 0, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %18 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %14, %dx.types.ResourceProperties { i32 13, i32 60 })  ; AnnotateHandle(res,props)  resource: CBuffer
  %19 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %15, %dx.types.ResourceProperties { i32 13, i32 348 })  ; AnnotateHandle(res,props)  resource: CBuffer
  %20 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %16, %dx.types.ResourceProperties { i32 13, i32 10076 })  ; AnnotateHandle(res,props)  resource: CBuffer
  %21 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %17, %dx.types.ResourceProperties { i32 13, i32 3764 })  ; AnnotateHandle(res,props)  resource: CBuffer
  %22 = call i32 @dx.op.loadInput.i32(i32 4, i32 3, i32 0, i8 0, i32 undef)  ; LoadInput(inputSigId,rowIndex,colIndex,gsVertexAxis)
  %23 = call i32 @dx.op.loadInput.i32(i32 4, i32 2, i32 0, i8 0, i32 undef)  ; LoadInput(inputSigId,rowIndex,colIndex,gsVertexAxis)
  %24 = call i32 @dx.op.loadInput.i32(i32 4, i32 1, i32 0, i8 0, i32 undef)  ; LoadInput(inputSigId,rowIndex,colIndex,gsVertexAxis)
  %25 = call float @dx.op.loadInput.f32(i32 4, i32 0, i32 0, i8 0, i32 undef), !dx.precise !51  ; LoadInput(inputSigId,rowIndex,colIndex,gsVertexAxis)
  %26 = call float @dx.op.loadInput.f32(i32 4, i32 0, i32 0, i8 1, i32 undef), !dx.precise !51  ; LoadInput(inputSigId,rowIndex,colIndex,gsVertexAxis)
  %27 = call float @dx.op.loadInput.f32(i32 4, i32 0, i32 0, i8 2, i32 undef), !dx.precise !51  ; LoadInput(inputSigId,rowIndex,colIndex,gsVertexAxis)
  %28 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %20, i32 0)  ; CBufferLoadLegacy(handle,regIndex)
  %29 = extractvalue %dx.types.CBufRet.f32 %28, 0
  %30 = extractvalue %dx.types.CBufRet.f32 %28, 1
  %31 = extractvalue %dx.types.CBufRet.f32 %28, 2
  %32 = extractvalue %dx.types.CBufRet.f32 %28, 3
  %33 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %20, i32 1)  ; CBufferLoadLegacy(handle,regIndex)
  %34 = extractvalue %dx.types.CBufRet.f32 %33, 0
  %35 = extractvalue %dx.types.CBufRet.f32 %33, 1
  %36 = extractvalue %dx.types.CBufRet.f32 %33, 2
  %37 = extractvalue %dx.types.CBufRet.f32 %33, 3
  %38 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %20, i32 2)  ; CBufferLoadLegacy(handle,regIndex)
  %39 = extractvalue %dx.types.CBufRet.f32 %38, 0
  %40 = extractvalue %dx.types.CBufRet.f32 %38, 1
  %41 = extractvalue %dx.types.CBufRet.f32 %38, 2
  %42 = extractvalue %dx.types.CBufRet.f32 %38, 3
  %43 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %20, i32 3)  ; CBufferLoadLegacy(handle,regIndex)
  %44 = extractvalue %dx.types.CBufRet.f32 %43, 0
  %45 = extractvalue %dx.types.CBufRet.f32 %43, 1
  %46 = extractvalue %dx.types.CBufRet.f32 %43, 2
  %47 = extractvalue %dx.types.CBufRet.f32 %43, 3
  %48 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %20, i32 64)  ; CBufferLoadLegacy(handle,regIndex)
  %49 = extractvalue %dx.types.CBufRet.f32 %48, 0
  %50 = extractvalue %dx.types.CBufRet.f32 %48, 1
  %51 = extractvalue %dx.types.CBufRet.f32 %48, 2
  %52 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %20, i32 65)  ; CBufferLoadLegacy(handle,regIndex)
  %53 = extractvalue %dx.types.CBufRet.f32 %52, 0
  %54 = extractvalue %dx.types.CBufRet.f32 %52, 1
  %55 = extractvalue %dx.types.CBufRet.f32 %52, 2
  %56 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %20, i32 66)  ; CBufferLoadLegacy(handle,regIndex)
  %57 = extractvalue %dx.types.CBufRet.f32 %56, 0
  %58 = extractvalue %dx.types.CBufRet.f32 %56, 1
  %59 = extractvalue %dx.types.CBufRet.f32 %56, 2
  %60 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %20, i32 72)  ; CBufferLoadLegacy(handle,regIndex)
  %61 = extractvalue %dx.types.CBufRet.f32 %60, 2
  %62 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %20, i32 73)  ; CBufferLoadLegacy(handle,regIndex)
  %63 = extractvalue %dx.types.CBufRet.f32 %62, 0
  %64 = extractvalue %dx.types.CBufRet.f32 %62, 1
  %65 = extractvalue %dx.types.CBufRet.f32 %62, 2
  %66 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %20, i32 80)  ; CBufferLoadLegacy(handle,regIndex)
  %67 = extractvalue %dx.types.CBufRet.f32 %66, 2
  %68 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %20, i32 81)  ; CBufferLoadLegacy(handle,regIndex)
  %69 = extractvalue %dx.types.CBufRet.f32 %68, 0
  %70 = extractvalue %dx.types.CBufRet.f32 %68, 1
  %71 = extractvalue %dx.types.CBufRet.f32 %68, 2
  %72 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %20, i32 84)  ; CBufferLoadLegacy(handle,regIndex)
  %73 = extractvalue %dx.types.CBufRet.f32 %72, 0
  %74 = extractvalue %dx.types.CBufRet.f32 %72, 1
  %75 = extractvalue %dx.types.CBufRet.f32 %72, 2
  %76 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %20, i32 85)  ; CBufferLoadLegacy(handle,regIndex)
  %77 = extractvalue %dx.types.CBufRet.f32 %76, 0
  %78 = extractvalue %dx.types.CBufRet.f32 %76, 1
  %79 = extractvalue %dx.types.CBufRet.f32 %76, 2
  %80 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %20, i32 148)  ; CBufferLoadLegacy(handle,regIndex)
  %81 = extractvalue %dx.types.CBufRet.f32 %80, 0
  %82 = extractvalue %dx.types.CBufRet.f32 %80, 1
  %83 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %20, i32 156)  ; CBufferLoadLegacy(handle,regIndex)
  %84 = extractvalue %dx.types.CBufRet.f32 %83, 3
  %85 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %20, i32 200)  ; CBufferLoadLegacy(handle,regIndex)
  %86 = extractvalue %dx.types.CBufRet.f32 %85, 1
  %87 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %20, i32 201)  ; CBufferLoadLegacy(handle,regIndex)
  %88 = extractvalue %dx.types.CBufRet.f32 %87, 0
  %89 = extractvalue %dx.types.CBufRet.f32 %87, 1
  %90 = extractvalue %dx.types.CBufRet.f32 %87, 2
  %91 = extractvalue %dx.types.CBufRet.f32 %87, 3
  %92 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %20, i32 202)  ; CBufferLoadLegacy(handle,regIndex)
  %93 = extractvalue %dx.types.CBufRet.f32 %92, 0
  %94 = extractvalue %dx.types.CBufRet.f32 %92, 1
  %95 = extractvalue %dx.types.CBufRet.f32 %92, 2
  %96 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %20, i32 203)  ; CBufferLoadLegacy(handle,regIndex)
  %97 = extractvalue %dx.types.CBufRet.f32 %96, 0
  %98 = extractvalue %dx.types.CBufRet.f32 %96, 1
  %99 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %20, i32 204)  ; CBufferLoadLegacy(handle,regIndex)
  %100 = extractvalue %dx.types.CBufRet.f32 %99, 3
  %101 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %20, i32 205)  ; CBufferLoadLegacy(handle,regIndex)
  %102 = extractvalue %dx.types.CBufRet.f32 %101, 0
  %103 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %20, i32 259)  ; CBufferLoadLegacy(handle,regIndex)
  %104 = extractvalue %dx.types.CBufRet.f32 %103, 0
  %105 = call %dx.types.CBufRet.i32 @dx.op.cbufferLoadLegacy.i32(i32 59, %dx.types.Handle %20, i32 321)  ; CBufferLoadLegacy(handle,regIndex)
  %106 = extractvalue %dx.types.CBufRet.i32 %105, 0
  %107 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %20, i32 182)  ; CBufferLoadLegacy(handle,regIndex)
  %108 = extractvalue %dx.types.CBufRet.f32 %107, 0
  %109 = extractvalue %dx.types.CBufRet.f32 %107, 1
  %110 = extractvalue %dx.types.CBufRet.f32 %107, 2
  %111 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %20, i32 183)  ; CBufferLoadLegacy(handle,regIndex)
  %112 = extractvalue %dx.types.CBufRet.f32 %111, 0
  %113 = extractvalue %dx.types.CBufRet.f32 %111, 1
  %114 = extractvalue %dx.types.CBufRet.f32 %111, 2
  %115 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %20, i32 184)  ; CBufferLoadLegacy(handle,regIndex)
  %116 = extractvalue %dx.types.CBufRet.f32 %115, 0
  %117 = extractvalue %dx.types.CBufRet.f32 %115, 1
  %118 = extractvalue %dx.types.CBufRet.f32 %115, 2
  %119 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %20, i32 185)  ; CBufferLoadLegacy(handle,regIndex)
  %120 = extractvalue %dx.types.CBufRet.f32 %119, 0
  %121 = extractvalue %dx.types.CBufRet.f32 %119, 1
  %122 = extractvalue %dx.types.CBufRet.f32 %119, 2
  %123 = extractvalue %dx.types.CBufRet.f32 %119, 3
  %124 = icmp slt i32 %24, 0
  br i1 %124, label %125, label %133

; <label>:125                                     ; preds = %0
  %126 = and i32 %24, 2147483647
  %127 = mul i32 %126, 44
  %128 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %2, %dx.types.ResourceProperties { i32 12, i32 16 })  ; AnnotateHandle(res,props)  resource: StructuredBuffer<stride=16>
  %129 = call %dx.types.ResRet.f32 @dx.op.rawBufferLoad.f32(i32 139, %dx.types.Handle %128, i32 %127, i32 0, i8 15, i32 4)  ; RawBufferLoad(srv,index,elementOffset,mask,alignment)
  %130 = extractvalue %dx.types.ResRet.f32 %129, 1
  %131 = bitcast float %130 to i32
  %132 = add i32 %131, %23
  br label %139

; <label>:133                                     ; preds = %0
  %134 = add i32 %24, %23
  %135 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %4, %dx.types.ResourceProperties { i32 12, i32 4 })  ; AnnotateHandle(res,props)  resource: StructuredBuffer<stride=4>
  %136 = call %dx.types.ResRet.i32 @dx.op.rawBufferLoad.i32(i32 139, %dx.types.Handle %135, i32 %134, i32 0, i8 1, i32 4)  ; RawBufferLoad(srv,index,elementOffset,mask,alignment)
  %137 = extractvalue %dx.types.ResRet.i32 %136, 0
  %138 = and i32 %137, 16777215
  br label %139

; <label>:139                                     ; preds = %133, %125
  %140 = phi i32 [ %132, %125 ], [ %138, %133 ]
  %141 = call %dx.types.CBufRet.i32 @dx.op.cbufferLoadLegacy.i32(i32 59, %dx.types.Handle %19, i32 0)  ; CBufferLoadLegacy(handle,regIndex)
  %142 = extractvalue %dx.types.CBufRet.i32 %141, 0
  %143 = extractvalue %dx.types.CBufRet.i32 %141, 1
  %144 = extractvalue %dx.types.CBufRet.i32 %141, 2
  %145 = and i32 %142, 31
  %146 = lshr i32 %140, %145
  %147 = and i32 %143, %140
  %148 = mul i32 %144, %146
  %149 = add i32 %148, %147
  %150 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %3, %dx.types.ResourceProperties { i32 12, i32 16 })  ; AnnotateHandle(res,props)  resource: StructuredBuffer<stride=16>
  %151 = call %dx.types.ResRet.f32 @dx.op.rawBufferLoad.f32(i32 139, %dx.types.Handle %150, i32 %149, i32 0, i8 15, i32 4)  ; RawBufferLoad(srv,index,elementOffset,mask,alignment)
  %152 = extractvalue %dx.types.ResRet.f32 %151, 0
  %153 = bitcast float %152 to i32
  %154 = and i32 %153, 1048575
  %155 = lshr i32 %153, 20
  %156 = icmp ne i32 %154, 1048575
  %157 = and i32 %155, 1024
  %158 = icmp eq i32 %157, 0
  %159 = and i1 %156, %158
  %160 = mul nuw nsw i32 %154, 44
  br i1 %159, label %161, label %281, !dx.controlflow.hints !52

; <label>:161                                     ; preds = %139
  %162 = or i32 %160, 1
  %163 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %2, %dx.types.ResourceProperties { i32 12, i32 16 })  ; AnnotateHandle(res,props)  resource: StructuredBuffer<stride=16>
  %164 = call %dx.types.ResRet.f32 @dx.op.rawBufferLoad.f32(i32 139, %dx.types.Handle %163, i32 %162, i32 0, i8 15, i32 4)  ; RawBufferLoad(srv,index,elementOffset,mask,alignment)
  %165 = extractvalue %dx.types.ResRet.f32 %164, 0
  %166 = extractvalue %dx.types.ResRet.f32 %164, 1
  %167 = extractvalue %dx.types.ResRet.f32 %164, 2
  %168 = shl i32 1, %145
  %169 = add i32 %147, %168
  %170 = add i32 %169, %148
  %171 = call %dx.types.ResRet.f32 @dx.op.rawBufferLoad.f32(i32 139, %dx.types.Handle %150, i32 %170, i32 0, i8 15, i32 4)  ; RawBufferLoad(srv,index,elementOffset,mask,alignment)
  %172 = extractvalue %dx.types.ResRet.f32 %171, 0
  %173 = extractvalue %dx.types.ResRet.f32 %171, 1
  %174 = extractvalue %dx.types.ResRet.f32 %171, 2
  %175 = extractvalue %dx.types.ResRet.f32 %171, 3
  %176 = bitcast float %172 to i32
  %177 = bitcast float %173 to i32
  %178 = bitcast float %174 to i32
  %179 = bitcast float %175 to i32
  %180 = shl i32 2, %145
  %181 = add i32 %147, %180
  %182 = add i32 %181, %148
  %183 = call %dx.types.ResRet.f32 @dx.op.rawBufferLoad.f32(i32 139, %dx.types.Handle %150, i32 %182, i32 0, i8 15, i32 4)  ; RawBufferLoad(srv,index,elementOffset,mask,alignment)
  %184 = extractvalue %dx.types.ResRet.f32 %183, 0
  %185 = extractvalue %dx.types.ResRet.f32 %183, 1
  %186 = extractvalue %dx.types.ResRet.f32 %183, 2
  %187 = and i32 %176, 65535
  %188 = uitofp i32 %187 to float
  %189 = lshr i32 %176, 16
  %190 = uitofp i32 %189 to float
  %191 = and i32 %177, 32767
  %192 = uitofp i32 %191 to float
  %193 = fadd float %188, -3.276800e+04
  %194 = fadd float %190, -3.276800e+04
  %195 = fmul float %193, 0x3F00002000000000
  %196 = fmul float %194, 0x3F00002000000000
  %197 = fadd float %192, -1.638400e+04
  %198 = fmul float %197, 0x3F06A0F8E0000000
  %199 = and i32 %177, 32768
  %200 = icmp ne i32 %199, 0
  %201 = fadd float %195, %196
  %202 = fsub float %195, %196
  %203 = call float @dx.op.unary.f32(i32 6, float %201), !dx.precise !51  ; FAbs(value)
  %204 = call float @dx.op.unary.f32(i32 6, float %202), !dx.precise !51  ; FAbs(value)
  %205 = call float @dx.op.dot2.f32(i32 54, float 1.000000e+00, float 1.000000e+00, float %203, float %204), !dx.precise !51  ; Dot2(ax,ay,bx,by)
  %206 = fsub float 2.000000e+00, %205
  %207 = call float @dx.op.dot3.f32(i32 55, float %201, float %202, float %206, float %201, float %202, float %206), !dx.precise !51  ; Dot3(ax,ay,az,bx,by,bz)
  %208 = call float @dx.op.unary.f32(i32 25, float %207), !dx.precise !51  ; Rsqrt(value)
  %209 = fmul float %201, %208
  %210 = fmul float %202, %208
  %211 = fmul float %206, %208
  %212 = fadd float %211, 1.000000e+00
  %213 = fdiv float 1.000000e+00, %212
  %214 = fmul float %209, %210
  %215 = fmul float %213, %214
  %216 = fsub float -0.000000e+00, %215
  %217 = fmul float %209, %209
  %218 = fmul float %213, %217
  %219 = fsub float 1.000000e+00, %218
  %220 = fmul float %210, %210
  %221 = fmul float %213, %220
  %222 = fsub float 1.000000e+00, %221
  %223 = fmul float %198, %198
  %224 = fsub float 1.000000e+00, %223
  %225 = call float @dx.op.unary.f32(i32 24, float %224), !dx.precise !51  ; Sqrt(value)
  %226 = select i1 %200, float %198, float %225
  %227 = select i1 %200, float %225, float %198
  %228 = fmul float %226, %219
  %229 = fmul float %226, %216
  %230 = fmul float %209, %226
  %231 = fmul float %227, %216
  %232 = fmul float %227, %222
  %233 = fmul float %210, %227
  %234 = fsub float -0.000000e+00, %233
  %235 = fadd float %231, %228
  %236 = fadd float %229, %232
  %237 = fsub float %234, %230
  %238 = fmul float %210, %237
  %239 = fmul float %211, %236
  %240 = fsub float %238, %239
  %241 = fmul float %211, %235
  %242 = fmul float %209, %237
  %243 = fsub float %241, %242
  %244 = fmul float %209, %236
  %245 = fmul float %210, %235
  %246 = fsub float %244, %245
  %247 = lshr i32 %179, 16
  %248 = shl i32 %247, 23
  %249 = add i32 %248, -125829120
  %250 = bitcast i32 %249 to float
  %251 = lshr i32 %178, 16
  %252 = and i32 %178, 65535
  %253 = and i32 %179, 65535
  %254 = uitofp i32 %252 to float
  %255 = uitofp i32 %251 to float
  %256 = uitofp i32 %253 to float
  %257 = fadd float %254, -3.276800e+04
  %258 = fadd float %255, -3.276800e+04
  %259 = fadd float %256, -3.276800e+04
  %260 = fmul float %257, %250
  %261 = fmul float %258, %250
  %262 = fmul float %259, %250
  %263 = fmul float %260, %235
  %264 = fmul float %260, %236
  %265 = fmul float %260, %237
  %266 = fmul float %261, %240
  %267 = fmul float %261, %243
  %268 = fmul float %261, %246
  %269 = fmul float %262, %209
  %270 = fmul float %262, %210
  %271 = fmul float %262, %211
  %272 = call float @dx.op.unary.f32(i32 6, float %260)  ; FAbs(value)
  %273 = call float @dx.op.unary.f32(i32 6, float %261)  ; FAbs(value)
  %274 = call float @dx.op.unary.f32(i32 6, float %262)  ; FAbs(value)
  %275 = fdiv fast float 1.000000e+00, %272
  %276 = fdiv fast float 1.000000e+00, %273
  %277 = fdiv fast float 1.000000e+00, %274
  %278 = and i32 %155, 1
  %279 = icmp ne i32 %278, 0
  %280 = select i1 %279, float -1.000000e+00, float 1.000000e+00
  br label %281

; <label>:281                                     ; preds = %161, %139
  %282 = phi float [ %275, %161 ], [ 0.000000e+00, %139 ]
  %283 = phi float [ %276, %161 ], [ 0.000000e+00, %139 ]
  %284 = phi float [ %277, %161 ], [ 0.000000e+00, %139 ]
  %285 = phi float [ %280, %161 ], [ 0.000000e+00, %139 ]
  %286 = phi float [ %165, %161 ], [ 0.000000e+00, %139 ]
  %287 = phi float [ %166, %161 ], [ 0.000000e+00, %139 ]
  %288 = phi float [ %167, %161 ], [ 0.000000e+00, %139 ]
  %289 = phi float [ %263, %161 ], [ 0.000000e+00, %139 ]
  %290 = phi float [ %264, %161 ], [ 0.000000e+00, %139 ]
  %291 = phi float [ %265, %161 ], [ 0.000000e+00, %139 ]
  %292 = phi float [ %266, %161 ], [ 0.000000e+00, %139 ]
  %293 = phi float [ %267, %161 ], [ 0.000000e+00, %139 ]
  %294 = phi float [ %268, %161 ], [ 0.000000e+00, %139 ]
  %295 = phi float [ %269, %161 ], [ 0.000000e+00, %139 ]
  %296 = phi float [ %270, %161 ], [ 0.000000e+00, %139 ]
  %297 = phi float [ %271, %161 ], [ 0.000000e+00, %139 ]
  %298 = phi float [ %184, %161 ], [ 0.000000e+00, %139 ]
  %299 = phi float [ %185, %161 ], [ 0.000000e+00, %139 ]
  %300 = phi float [ %186, %161 ], [ 0.000000e+00, %139 ]
  %301 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %2, %dx.types.ResourceProperties { i32 12, i32 16 })  ; AnnotateHandle(res,props)  resource: StructuredBuffer<stride=16>
  %302 = call %dx.types.ResRet.f32 @dx.op.rawBufferLoad.f32(i32 139, %dx.types.Handle %301, i32 %160, i32 0, i8 15, i32 4)  ; RawBufferLoad(srv,index,elementOffset,mask,alignment)
  %303 = extractvalue %dx.types.ResRet.f32 %302, 0
  %304 = bitcast float %303 to i32
  %305 = call %dx.types.CBufRet.i32 @dx.op.cbufferLoadLegacy.i32(i32 59, %dx.types.Handle %18, i32 0)  ; CBufferLoadLegacy(handle,regIndex)
  %306 = extractvalue %dx.types.CBufRet.i32 %305, 3
  %307 = add i32 %306, %22
  %308 = shl i32 %307, 1
  %309 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %1, %dx.types.ResourceProperties { i32 10, i32 1033 })  ; AnnotateHandle(res,props)  resource: TypedBuffer<4xF32>
  %310 = call %dx.types.ResRet.f32 @dx.op.bufferLoad.f32(i32 68, %dx.types.Handle %309, i32 %308, i32 undef)  ; BufferLoad(srv,index,wot)
  %311 = extractvalue %dx.types.ResRet.f32 %310, 0
  %312 = extractvalue %dx.types.ResRet.f32 %310, 1
  %313 = extractvalue %dx.types.ResRet.f32 %310, 2
  %314 = or i32 %308, 1
  %315 = call %dx.types.ResRet.f32 @dx.op.bufferLoad.f32(i32 68, %dx.types.Handle %309, i32 %314, i32 undef)  ; BufferLoad(srv,index,wot)
  %316 = extractvalue %dx.types.ResRet.f32 %315, 0
  %317 = extractvalue %dx.types.ResRet.f32 %315, 1
  %318 = extractvalue %dx.types.ResRet.f32 %315, 2
  %319 = extractvalue %dx.types.ResRet.f32 %315, 3
  %320 = fmul fast float %317, %313
  %321 = fmul fast float %318, %312
  %322 = fsub fast float %320, %321
  %323 = fmul fast float %318, %311
  %324 = fmul fast float %316, %313
  %325 = fsub fast float %323, %324
  %326 = fmul fast float %316, %312
  %327 = fmul fast float %317, %311
  %328 = fsub fast float %326, %327
  %329 = fmul fast float %322, %319
  %330 = fmul fast float %325, %319
  %331 = fmul fast float %328, %319
  %332 = fmul fast float %330, %318
  %333 = fmul fast float %331, %317
  %334 = fsub fast float %332, %333
  %335 = fmul fast float %331, %316
  %336 = fmul fast float %329, %318
  %337 = fsub fast float %335, %336
  %338 = fmul fast float %329, %317
  %339 = fmul fast float %330, %316
  %340 = fsub fast float %338, %339
  %341 = fmul fast float %334, %319
  %342 = fmul fast float %337, %319
  %343 = fmul fast float %340, %319
  %344 = fmul fast float %289, %282
  %345 = fmul fast float %290, %282
  %346 = fmul fast float %291, %282
  %347 = fmul fast float %292, %283
  %348 = fmul fast float %293, %283
  %349 = fmul fast float %294, %283
  %350 = fmul fast float %295, %284
  %351 = fmul fast float %296, %284
  %352 = fmul fast float %297, %284
  %353 = fmul fast float %341, %344
  %354 = call float @dx.op.tertiary.f32(i32 46, float %342, float %347, float %353)  ; FMad(a,b,c)
  %355 = call float @dx.op.tertiary.f32(i32 46, float %343, float %350, float %354)  ; FMad(a,b,c)
  %356 = fmul fast float %341, %345
  %357 = call float @dx.op.tertiary.f32(i32 46, float %342, float %348, float %356)  ; FMad(a,b,c)
  %358 = call float @dx.op.tertiary.f32(i32 46, float %343, float %351, float %357)  ; FMad(a,b,c)
  %359 = fmul fast float %341, %346
  %360 = call float @dx.op.tertiary.f32(i32 46, float %342, float %349, float %359)  ; FMad(a,b,c)
  %361 = call float @dx.op.tertiary.f32(i32 46, float %343, float %352, float %360)  ; FMad(a,b,c)
  %362 = fmul fast float %316, %344
  %363 = call float @dx.op.tertiary.f32(i32 46, float %317, float %347, float %362)  ; FMad(a,b,c)
  %364 = call float @dx.op.tertiary.f32(i32 46, float %318, float %350, float %363)  ; FMad(a,b,c)
  %365 = fmul fast float %316, %345
  %366 = call float @dx.op.tertiary.f32(i32 46, float %317, float %348, float %365)  ; FMad(a,b,c)
  %367 = call float @dx.op.tertiary.f32(i32 46, float %318, float %351, float %366)  ; FMad(a,b,c)
  %368 = fmul fast float %316, %346
  %369 = call float @dx.op.tertiary.f32(i32 46, float %317, float %349, float %368)  ; FMad(a,b,c)
  %370 = call float @dx.op.tertiary.f32(i32 46, float %318, float %352, float %369)  ; FMad(a,b,c)
  %371 = fmul fast float %319, %285
  %372 = fmul float %27, %295
  %373 = fmul float %27, %296
  %374 = fmul float %27, %297
  %375 = call float @dx.op.tertiary.f32(i32 46, float %26, float %292, float %372), !dx.precise !51  ; FMad(a,b,c)
  %376 = call float @dx.op.tertiary.f32(i32 46, float %26, float %293, float %373), !dx.precise !51  ; FMad(a,b,c)
  %377 = call float @dx.op.tertiary.f32(i32 46, float %26, float %294, float %374), !dx.precise !51  ; FMad(a,b,c)
  %378 = call float @dx.op.tertiary.f32(i32 46, float %25, float %289, float %375), !dx.precise !51  ; FMad(a,b,c)
  %379 = call float @dx.op.tertiary.f32(i32 46, float %25, float %290, float %376), !dx.precise !51  ; FMad(a,b,c)
  %380 = call float @dx.op.tertiary.f32(i32 46, float %25, float %291, float %377), !dx.precise !51  ; FMad(a,b,c)
  %381 = fadd float %73, %286
  %382 = fadd float %74, %287
  %383 = fadd float %75, %288
  %384 = fadd float %77, %381
  %385 = fadd float %78, %382
  %386 = fadd float %79, %383
  %387 = fadd float %384, %298
  %388 = fadd float %385, %299
  %389 = fadd float %386, %300
  %390 = fadd float %387, %378
  %391 = fadd float %388, %379
  %392 = fadd float %389, %380
  %393 = fadd float %390, 0.000000e+00
  %394 = fadd float %391, 0.000000e+00
  %395 = fadd float %392, 0.000000e+00
  %396 = and i32 %304, 1073741824
  %397 = icmp eq i32 %396, 0
  br i1 %397, label %414, label %398, !dx.controlflow.hints !53

; <label>:398                                     ; preds = %281
  %399 = fmul float %49, %393
  %400 = call float @dx.op.tertiary.f32(i32 46, float %394, float %53, float %399), !dx.precise !51  ; FMad(a,b,c)
  %401 = call float @dx.op.tertiary.f32(i32 46, float %395, float %57, float %400), !dx.precise !51  ; FMad(a,b,c)
  %402 = fmul float %50, %393
  %403 = call float @dx.op.tertiary.f32(i32 46, float %394, float %54, float %402), !dx.precise !51  ; FMad(a,b,c)
  %404 = call float @dx.op.tertiary.f32(i32 46, float %395, float %58, float %403), !dx.precise !51  ; FMad(a,b,c)
  %405 = fmul float %51, %393
  %406 = call float @dx.op.tertiary.f32(i32 46, float %394, float %55, float %405), !dx.precise !51  ; FMad(a,b,c)
  %407 = call float @dx.op.tertiary.f32(i32 46, float %395, float %59, float %406), !dx.precise !51  ; FMad(a,b,c)
  %408 = fsub float %401, %393
  %409 = fsub float %404, %394
  %410 = fsub float %407, %395
  %411 = fadd float %393, %408
  %412 = fadd float %394, %409
  %413 = fadd float %395, %410
  br label %414

; <label>:414                                     ; preds = %398, %281
  %415 = phi float [ %411, %398 ], [ %393, %281 ]
  %416 = phi float [ %412, %398 ], [ %394, %281 ]
  %417 = phi float [ %413, %398 ], [ %395, %281 ]
  %418 = fmul float %29, %415
  %419 = call float @dx.op.tertiary.f32(i32 46, float %416, float %34, float %418), !dx.precise !51  ; FMad(a,b,c)
  %420 = call float @dx.op.tertiary.f32(i32 46, float %417, float %39, float %419), !dx.precise !51  ; FMad(a,b,c)
  %421 = call float @dx.op.tertiary.f32(i32 46, float 1.000000e+00, float %44, float %420), !dx.precise !51  ; FMad(a,b,c)
  %422 = fmul float %30, %415
  %423 = call float @dx.op.tertiary.f32(i32 46, float %416, float %35, float %422), !dx.precise !51  ; FMad(a,b,c)
  %424 = call float @dx.op.tertiary.f32(i32 46, float %417, float %40, float %423), !dx.precise !51  ; FMad(a,b,c)
  %425 = call float @dx.op.tertiary.f32(i32 46, float 1.000000e+00, float %45, float %424), !dx.precise !51  ; FMad(a,b,c)
  %426 = fmul float %31, %415
  %427 = call float @dx.op.tertiary.f32(i32 46, float %416, float %36, float %426), !dx.precise !51  ; FMad(a,b,c)
  %428 = call float @dx.op.tertiary.f32(i32 46, float %417, float %41, float %427), !dx.precise !51  ; FMad(a,b,c)
  %429 = call float @dx.op.tertiary.f32(i32 46, float 1.000000e+00, float %46, float %428), !dx.precise !51  ; FMad(a,b,c)
  %430 = fmul float %32, %415
  %431 = call float @dx.op.tertiary.f32(i32 46, float %416, float %37, float %430), !dx.precise !51  ; FMad(a,b,c)
  %432 = call float @dx.op.tertiary.f32(i32 46, float %417, float %42, float %431), !dx.precise !51  ; FMad(a,b,c)
  %433 = call float @dx.op.tertiary.f32(i32 46, float 1.000000e+00, float %47, float %432), !dx.precise !51  ; FMad(a,b,c)
  %434 = fsub fast float %415, %69
  %435 = fsub fast float %416, %70
  %436 = fsub fast float %417, %71
  %437 = fcmp fast oeq float %100, 0.000000e+00
  %438 = and i32 %106, 32
  %439 = icmp eq i32 %438, 0
  %440 = and i1 %437, %439
  br i1 %440, label %696, label %441

; <label>:441                                     ; preds = %414
  %442 = call float @dx.op.dot3.f32(i32 55, float %434, float %435, float %436, float %434, float %435, float %436)  ; Dot3(ax,ay,az,bx,by,bz)
  %443 = call float @dx.op.unary.f32(i32 25, float %442)  ; Rsqrt(value)
  %444 = fmul fast float %443, %434
  %445 = fmul fast float %443, %435
  %446 = fmul fast float %443, %436
  %447 = call float @dx.op.dot3.f32(i32 55, float %444, float %445, float %446, float %63, float %64, float %65)  ; Dot3(ax,ay,az,bx,by,bz)
  %448 = fcmp fast ogt float %447, 0x3F50624DE0000000
  %449 = fdiv fast float 1.000000e+00, %447
  %450 = select i1 %448, float %449, float 0.000000e+00
  %451 = fmul fast float %450, %104
  %452 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %21, i32 71)  ; CBufferLoadLegacy(handle,regIndex)
  %453 = extractvalue %dx.types.CBufRet.f32 %452, 0
  %454 = extractvalue %dx.types.CBufRet.f32 %452, 1
  %455 = extractvalue %dx.types.CBufRet.f32 %452, 2
  %456 = extractvalue %dx.types.CBufRet.f32 %452, 3
  %457 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %21, i32 72)  ; CBufferLoadLegacy(handle,regIndex)
  %458 = extractvalue %dx.types.CBufRet.f32 %457, 0
  %459 = extractvalue %dx.types.CBufRet.f32 %457, 1
  %460 = extractvalue %dx.types.CBufRet.f32 %457, 2
  %461 = extractvalue %dx.types.CBufRet.f32 %457, 3
  %462 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %21, i32 73)  ; CBufferLoadLegacy(handle,regIndex)
  %463 = extractvalue %dx.types.CBufRet.f32 %462, 0
  %464 = extractvalue %dx.types.CBufRet.f32 %462, 1
  %465 = extractvalue %dx.types.CBufRet.f32 %462, 2
  %466 = extractvalue %dx.types.CBufRet.f32 %462, 3
  %467 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %21, i32 74)  ; CBufferLoadLegacy(handle,regIndex)
  %468 = extractvalue %dx.types.CBufRet.f32 %467, 0
  %469 = extractvalue %dx.types.CBufRet.f32 %467, 1
  %470 = extractvalue %dx.types.CBufRet.f32 %467, 2
  %471 = extractvalue %dx.types.CBufRet.f32 %467, 3
  %472 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %21, i32 75)  ; CBufferLoadLegacy(handle,regIndex)
  %473 = extractvalue %dx.types.CBufRet.f32 %472, 0
  %474 = extractvalue %dx.types.CBufRet.f32 %472, 1
  %475 = extractvalue %dx.types.CBufRet.f32 %472, 2
  %476 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %21, i32 76)  ; CBufferLoadLegacy(handle,regIndex)
  %477 = extractvalue %dx.types.CBufRet.f32 %476, 3
  %478 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %21, i32 77)  ; CBufferLoadLegacy(handle,regIndex)
  %479 = extractvalue %dx.types.CBufRet.f32 %478, 0
  %480 = extractvalue %dx.types.CBufRet.f32 %478, 1
  %481 = extractvalue %dx.types.CBufRet.f32 %478, 2
  %482 = extractvalue %dx.types.CBufRet.f32 %478, 3
  %483 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %21, i32 78)  ; CBufferLoadLegacy(handle,regIndex)
  %484 = extractvalue %dx.types.CBufRet.f32 %483, 0
  %485 = extractvalue %dx.types.CBufRet.f32 %483, 1
  %486 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %21, i32 79)  ; CBufferLoadLegacy(handle,regIndex)
  %487 = extractvalue %dx.types.CBufRet.f32 %486, 0
  %488 = extractvalue %dx.types.CBufRet.f32 %486, 1
  %489 = extractvalue %dx.types.CBufRet.f32 %486, 2
  %490 = extractvalue %dx.types.CBufRet.f32 %486, 3
  %491 = fadd fast float %67, %61
  %492 = call float @dx.op.binary.f32(i32 36, float %491, float %455)  ; FMin(a,b)
  %493 = call float @dx.op.dot2.f32(i32 54, float %434, float %435, float %434, float %435)  ; Dot2(ax,ay,bx,by)
  %494 = fcmp fast ogt float %490, 0.000000e+00
  %495 = fmul fast float %490, %490
  %496 = fcmp fast ogt float %493, %495
  %497 = and i1 %494, %496
  br i1 %497, label %498, label %505

; <label>:498                                     ; preds = %441
  %499 = call float @dx.op.binary.f32(i32 35, float 1.000000e+00, float %493)  ; FMax(a,b)
  %500 = call float @dx.op.unary.f32(i32 24, float %499)  ; Sqrt(value)
  %501 = fdiv fast float %490, %500
  %502 = fmul fast float %501, %434
  %503 = fmul fast float %501, %435
  %504 = fmul fast float %501, %436
  br label %505

; <label>:505                                     ; preds = %498, %441
  %506 = phi float [ %502, %498 ], [ %434, %441 ]
  %507 = phi float [ %503, %498 ], [ %435, %441 ]
  %508 = phi float [ %504, %498 ], [ %436, %441 ]
  %509 = fsub fast float %491, %492
  %510 = fadd fast float %508, %509
  %511 = call float @dx.op.dot3.f32(i32 55, float %506, float %507, float %510, float %506, float %507, float %510)  ; Dot3(ax,ay,az,bx,by,bz)
  %512 = call float @dx.op.binary.f32(i32 35, float %511, float 0x3E45798EE0000000)  ; FMax(a,b)
  %513 = call float @dx.op.unary.f32(i32 25, float %512)  ; Rsqrt(value)
  %514 = fmul fast float %513, %511
  %515 = fmul fast float %513, %506
  %516 = fmul fast float %513, %507
  %517 = fmul fast float %513, %510
  %518 = call float @dx.op.binary.f32(i32 35, float %451, float %456)  ; FMax(a,b)
  %519 = fcmp fast ogt float %518, 0.000000e+00
  br i1 %519, label %520, label %539

; <label>:520                                     ; preds = %505
  %521 = fmul fast float %518, %513
  %522 = fmul fast float %521, %510
  %523 = fadd fast float %522, %492
  %524 = fsub fast float %510, %522
  %525 = fsub fast float 1.000000e+00, %521
  %526 = fmul fast float %525, %514
  %527 = fsub fast float %523, %469
  %528 = fmul fast float %527, %454
  %529 = call float @dx.op.binary.f32(i32 35, float -1.270000e+02, float %528)  ; FMax(a,b)
  %530 = fsub fast float -0.000000e+00, %529
  %531 = call float @dx.op.unary.f32(i32 21, float %530)  ; Exp(value)
  %532 = fmul fast float %531, %468
  %533 = fsub fast float %523, %461
  %534 = fmul fast float %533, %459
  %535 = call float @dx.op.binary.f32(i32 35, float -1.270000e+02, float %534)  ; FMax(a,b)
  %536 = fsub fast float -0.000000e+00, %535
  %537 = call float @dx.op.unary.f32(i32 21, float %536)  ; Exp(value)
  %538 = fmul fast float %537, %460
  br label %539

; <label>:539                                     ; preds = %520, %505
  %540 = phi float [ %532, %520 ], [ %453, %505 ]
  %541 = phi float [ %538, %520 ], [ %458, %505 ]
  %542 = phi float [ %526, %520 ], [ %514, %505 ]
  %543 = phi float [ %524, %520 ], [ %510, %505 ]
  %544 = fmul fast float %543, %454
  %545 = call float @dx.op.binary.f32(i32 35, float -1.270000e+02, float %544)  ; FMax(a,b)
  %546 = fsub fast float -0.000000e+00, %545
  %547 = call float @dx.op.unary.f32(i32 21, float %546)  ; Exp(value)
  %548 = fsub fast float 1.000000e+00, %547
  %549 = fdiv fast float %548, %545
  %550 = fmul fast float %545, 0x3FCEBFBE00000000
  %551 = fsub fast float 0x3FE62E4300000000, %550
  %552 = call float @dx.op.unary.f32(i32 6, float %545)  ; FAbs(value)
  %553 = fcmp fast ogt float %552, 0x3F847AE140000000
  %554 = select i1 %553, float %549, float %551
  %555 = fmul fast float %554, %540
  %556 = fmul fast float %543, %459
  %557 = call float @dx.op.binary.f32(i32 35, float -1.270000e+02, float %556)  ; FMax(a,b)
  %558 = fsub fast float -0.000000e+00, %557
  %559 = call float @dx.op.unary.f32(i32 21, float %558)  ; Exp(value)
  %560 = fsub fast float 1.000000e+00, %559
  %561 = fdiv fast float %560, %557
  %562 = fmul fast float %557, 0x3FCEBFBE00000000
  %563 = fsub fast float 0x3FE62E4300000000, %562
  %564 = call float @dx.op.unary.f32(i32 6, float %557)  ; FAbs(value)
  %565 = fcmp fast ogt float %564, 0x3F847AE140000000
  %566 = select i1 %565, float %561, float %563
  %567 = fmul fast float %566, %541
  %568 = fadd fast float %567, %555
  %569 = fcmp fast ogt float %470, 0.000000e+00
  br i1 %569, label %570, label %599, !dx.controlflow.hints !54

; <label>:570                                     ; preds = %539
  %571 = fmul fast float %514, %487
  %572 = fadd fast float %571, %488
  %573 = call float @dx.op.unary.f32(i32 7, float %572)  ; Saturate(value)
  %574 = fsub fast float -0.000000e+00, %484
  %575 = call float @dx.op.dot2.f32(i32 54, float %506, float %507, float %485, float %574)  ; Dot2(ax,ay,bx,by)
  %576 = call float @dx.op.dot2.f32(i32 54, float %506, float %507, float %484, float %485)  ; Dot2(ax,ay,bx,by)
  %577 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %9, %dx.types.ResourceProperties { i32 5, i32 1033 })  ; AnnotateHandle(res,props)  resource: TextureCube<4xF32>
  %578 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %11, %dx.types.ResourceProperties { i32 14, i32 0 })  ; AnnotateHandle(res,props)  resource: SamplerState
  %579 = call %dx.types.ResRet.f32 @dx.op.sampleLevel.f32(i32 62, %dx.types.Handle %577, %dx.types.Handle %578, float %575, float %576, float %510, float undef, i32 undef, i32 undef, i32 undef, float 0.000000e+00)  ; SampleLevel(srv,sampler,coord0,coord1,coord2,coord3,offset0,offset1,offset2,LOD)
  %580 = extractvalue %dx.types.ResRet.f32 %579, 0
  %581 = extractvalue %dx.types.ResRet.f32 %579, 1
  %582 = extractvalue %dx.types.ResRet.f32 %579, 2
  %583 = call %dx.types.ResRet.f32 @dx.op.sampleLevel.f32(i32 62, %dx.types.Handle %577, %dx.types.Handle %578, float %575, float %576, float %510, float undef, i32 undef, i32 undef, i32 undef, float %489)  ; SampleLevel(srv,sampler,coord0,coord1,coord2,coord3,offset0,offset1,offset2,LOD)
  %584 = extractvalue %dx.types.ResRet.f32 %583, 0
  %585 = extractvalue %dx.types.ResRet.f32 %583, 1
  %586 = extractvalue %dx.types.ResRet.f32 %583, 2
  %587 = fsub fast float %580, %584
  %588 = fsub fast float %581, %585
  %589 = fsub fast float %582, %586
  %590 = fmul fast float %587, %573
  %591 = fmul fast float %588, %573
  %592 = fmul fast float %589, %573
  %593 = fadd fast float %590, %584
  %594 = fadd fast float %591, %585
  %595 = fadd fast float %592, %586
  %596 = fmul fast float %593, %463
  %597 = fmul fast float %594, %464
  %598 = fmul fast float %595, %465
  br label %599

; <label>:599                                     ; preds = %570, %539
  %600 = phi float [ %596, %570 ], [ %463, %539 ]
  %601 = phi float [ %597, %570 ], [ %464, %539 ]
  %602 = phi float [ %598, %570 ], [ %465, %539 ]
  %603 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %20, i32 200)  ; CBufferLoadLegacy(handle,regIndex)
  %604 = extractvalue %dx.types.CBufRet.f32 %603, 1
  %605 = fmul fast float %604, %473
  %606 = fmul fast float %604, %474
  %607 = fmul fast float %604, %475
  %608 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %5, %dx.types.ResourceProperties { i32 12, i32 16 })  ; AnnotateHandle(res,props)  resource: StructuredBuffer<stride=16>
  %609 = call %dx.types.ResRet.f32 @dx.op.rawBufferLoad.f32(i32 139, %dx.types.Handle %608, i32 0, i32 0, i8 15, i32 4)  ; RawBufferLoad(srv,index,elementOffset,mask,alignment)
  %610 = extractvalue %dx.types.ResRet.f32 %609, 0
  %611 = extractvalue %dx.types.ResRet.f32 %609, 1
  %612 = extractvalue %dx.types.ResRet.f32 %609, 2
  %613 = fmul fast float %605, %610
  %614 = fmul fast float %606, %611
  %615 = fmul fast float %607, %612
  %616 = fadd fast float %613, %600
  %617 = fadd fast float %614, %601
  %618 = fadd fast float %615, %602
  %619 = fcmp fast oge float %477, 0.000000e+00
  %620 = fcmp fast oeq float %470, 0.000000e+00
  %621 = and i1 %620, %619
  br i1 %621, label %622, label %673, !dx.controlflow.hints !55

; <label>:622                                     ; preds = %599
  %623 = fmul fast float %116, %86
  %624 = fmul fast float %117, %86
  %625 = fmul fast float %118, %86
  %626 = fadd fast float %479, %623
  %627 = fadd fast float %480, %624
  %628 = fadd fast float %481, %625
  %629 = call float @dx.op.dot3.f32(i32 55, float %515, float %516, float %517, float %108, float %109, float %110)  ; Dot3(ax,ay,az,bx,by,bz)
  %630 = call float @dx.op.unary.f32(i32 7, float %629)  ; Saturate(value)
  %631 = call float @dx.op.unary.f32(i32 23, float %630)  ; Log(value)
  %632 = fmul fast float %631, %482
  %633 = call float @dx.op.unary.f32(i32 21, float %632)  ; Exp(value)
  %634 = fmul fast float %633, %626
  %635 = fmul fast float %633, %627
  %636 = fmul fast float %633, %628
  %637 = fcmp fast ogt float %123, 0.000000e+00
  br i1 %637, label %638, label %656

; <label>:638                                     ; preds = %622
  %639 = fmul fast float %120, %86
  %640 = fmul fast float %121, %86
  %641 = fmul fast float %122, %86
  %642 = fadd fast float %479, %639
  %643 = fadd fast float %480, %640
  %644 = fadd fast float %481, %641
  %645 = call float @dx.op.dot3.f32(i32 55, float %515, float %516, float %517, float %112, float %113, float %114)  ; Dot3(ax,ay,az,bx,by,bz)
  %646 = call float @dx.op.unary.f32(i32 7, float %645)  ; Saturate(value)
  %647 = call float @dx.op.unary.f32(i32 23, float %646)  ; Log(value)
  %648 = fmul fast float %647, %482
  %649 = call float @dx.op.unary.f32(i32 21, float %648)  ; Exp(value)
  %650 = fmul fast float %649, %642
  %651 = fmul fast float %649, %643
  %652 = fmul fast float %649, %644
  %653 = fadd fast float %650, %634
  %654 = fadd fast float %651, %635
  %655 = fadd fast float %652, %636
  br label %656

; <label>:656                                     ; preds = %638, %622
  %657 = phi float [ %653, %638 ], [ %634, %622 ]
  %658 = phi float [ %654, %638 ], [ %635, %622 ]
  %659 = phi float [ %655, %638 ], [ %636, %622 ]
  %660 = fmul float %659, 0x3FB45F3060000000
  %661 = fmul float %658, 0x3FB45F3060000000
  %662 = fmul float %657, 0x3FB45F3060000000
  %663 = fsub fast float %542, %477
  %664 = call float @dx.op.binary.f32(i32 35, float %663, float 0.000000e+00)  ; FMax(a,b)
  %665 = fmul fast float %568, %664
  %666 = fsub fast float -0.000000e+00, %665
  %667 = call float @dx.op.unary.f32(i32 21, float %666)  ; Exp(value)
  %668 = call float @dx.op.unary.f32(i32 7, float %667)  ; Saturate(value)
  %669 = fsub fast float 1.000000e+00, %668
  %670 = fmul fast float %669, %662
  %671 = fmul fast float %669, %661
  %672 = fmul fast float %669, %660
  br label %673

; <label>:673                                     ; preds = %656, %599
  %674 = phi float [ %670, %656 ], [ 0.000000e+00, %599 ]
  %675 = phi float [ %671, %656 ], [ 0.000000e+00, %599 ]
  %676 = phi float [ %672, %656 ], [ 0.000000e+00, %599 ]
  %677 = fmul fast float %542, %568
  %678 = fsub fast float -0.000000e+00, %677
  %679 = call float @dx.op.unary.f32(i32 21, float %678)  ; Exp(value)
  %680 = call float @dx.op.unary.f32(i32 7, float %679)  ; Saturate(value)
  %681 = call float @dx.op.binary.f32(i32 35, float %680, float %466)  ; FMax(a,b)
  %682 = fcmp fast ogt float %471, 0.000000e+00
  %683 = fcmp fast ogt float %514, %471
  %684 = and i1 %682, %683
  %685 = select i1 %684, float 0.000000e+00, float %674
  %686 = select i1 %684, float 0.000000e+00, float %675
  %687 = select i1 %684, float 0.000000e+00, float %676
  %688 = select i1 %684, float 1.000000e+00, float %681
  %689 = fsub fast float 1.000000e+00, %688
  %690 = fmul fast float %689, %616
  %691 = fmul fast float %689, %617
  %692 = fmul fast float %689, %618
  %693 = fadd fast float %690, %685
  %694 = fadd fast float %691, %686
  %695 = fadd fast float %692, %687
  br label %696

; <label>:696                                     ; preds = %673, %414
  %697 = phi float [ %693, %673 ], [ 0.000000e+00, %414 ]
  %698 = phi float [ %694, %673 ], [ 0.000000e+00, %414 ]
  %699 = phi float [ %695, %673 ], [ 0.000000e+00, %414 ]
  %700 = phi float [ %688, %673 ], [ 1.000000e+00, %414 ]
  %701 = fcmp fast ogt float %98, 0.000000e+00
  br i1 %701, label %702, label %867

; <label>:702                                     ; preds = %696
  %703 = fcmp fast une float %102, 0.000000e+00
  %704 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %20, i32 204)  ; CBufferLoadLegacy(handle,regIndex)
  %705 = extractvalue %dx.types.CBufRet.f32 %704, 3
  %706 = fcmp fast oeq float %705, 0.000000e+00
  %707 = call %dx.types.CBufRet.i32 @dx.op.cbufferLoadLegacy.i32(i32 59, %dx.types.Handle %20, i32 321)  ; CBufferLoadLegacy(handle,regIndex)
  %708 = extractvalue %dx.types.CBufRet.i32 %707, 0
  %709 = and i32 %708, 8
  %710 = icmp eq i32 %709, 0
  %711 = and i1 %706, %710
  br i1 %711, label %855, label %712

; <label>:712                                     ; preds = %702
  %713 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %20, i32 31)  ; CBufferLoadLegacy(handle,regIndex)
  %714 = extractvalue %dx.types.CBufRet.f32 %713, 3
  %715 = fcmp fast ult float %714, 1.000000e+00
  br i1 %715, label %776, label %716

; <label>:716                                     ; preds = %712
  %717 = fmul fast float %421, 5.000000e-01
  %718 = fmul fast float %425, 5.000000e-01
  %719 = fadd fast float %717, 5.000000e-01
  %720 = fsub fast float 5.000000e-01, %718
  %721 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %20, i32 149)  ; CBufferLoadLegacy(handle,regIndex)
  %722 = extractvalue %dx.types.CBufRet.f32 %721, 0
  %723 = extractvalue %dx.types.CBufRet.f32 %721, 1
  %724 = fmul fast float %722, %719
  %725 = fmul fast float %723, %720
  %726 = fadd fast float %724, %81
  %727 = fadd fast float %725, %82
  %728 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %20, i32 44)  ; CBufferLoadLegacy(handle,regIndex)
  %729 = extractvalue %dx.types.CBufRet.f32 %728, 0
  %730 = extractvalue %dx.types.CBufRet.f32 %728, 1
  %731 = extractvalue %dx.types.CBufRet.f32 %728, 2
  %732 = extractvalue %dx.types.CBufRet.f32 %728, 3
  %733 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %20, i32 45)  ; CBufferLoadLegacy(handle,regIndex)
  %734 = extractvalue %dx.types.CBufRet.f32 %733, 0
  %735 = extractvalue %dx.types.CBufRet.f32 %733, 1
  %736 = extractvalue %dx.types.CBufRet.f32 %733, 2
  %737 = extractvalue %dx.types.CBufRet.f32 %733, 3
  %738 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %20, i32 46)  ; CBufferLoadLegacy(handle,regIndex)
  %739 = extractvalue %dx.types.CBufRet.f32 %738, 0
  %740 = extractvalue %dx.types.CBufRet.f32 %738, 1
  %741 = extractvalue %dx.types.CBufRet.f32 %738, 2
  %742 = extractvalue %dx.types.CBufRet.f32 %738, 3
  %743 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %20, i32 47)  ; CBufferLoadLegacy(handle,regIndex)
  %744 = extractvalue %dx.types.CBufRet.f32 %743, 0
  %745 = extractvalue %dx.types.CBufRet.f32 %743, 1
  %746 = extractvalue %dx.types.CBufRet.f32 %743, 2
  %747 = extractvalue %dx.types.CBufRet.f32 %743, 3
  %748 = fmul fast float %729, %726
  %749 = call float @dx.op.tertiary.f32(i32 46, float %727, float %734, float %748)  ; FMad(a,b,c)
  %750 = fadd fast float %749, %739
  %751 = fadd fast float %750, %744
  %752 = fmul fast float %730, %726
  %753 = call float @dx.op.tertiary.f32(i32 46, float %727, float %735, float %752)  ; FMad(a,b,c)
  %754 = fadd fast float %745, %740
  %755 = fadd fast float %754, %753
  %756 = fmul fast float %731, %726
  %757 = call float @dx.op.tertiary.f32(i32 46, float %727, float %736, float %756)  ; FMad(a,b,c)
  %758 = fadd fast float %746, %741
  %759 = fadd fast float %758, %757
  %760 = fmul fast float %732, %726
  %761 = call float @dx.op.tertiary.f32(i32 46, float %727, float %737, float %760)  ; FMad(a,b,c)
  %762 = fadd fast float %747, %742
  %763 = fadd fast float %762, %761
  %764 = fdiv fast float %751, %763
  %765 = fdiv fast float %755, %763
  %766 = fdiv fast float %759, %763
  %767 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %20, i32 37)  ; CBufferLoadLegacy(handle,regIndex)
  %768 = extractvalue %dx.types.CBufRet.f32 %767, 1
  %769 = fadd fast float %768, %766
  %770 = fmul fast float %434, 2.000000e+00
  %771 = fadd fast float %770, %764
  %772 = fmul fast float %435, 2.000000e+00
  %773 = fadd fast float %772, %765
  %774 = fmul fast float %436, 2.000000e+00
  %775 = fadd fast float %774, %769
  br label %776

; <label>:776                                     ; preds = %716, %712
  %777 = phi float [ %771, %716 ], [ %434, %712 ]
  %778 = phi float [ %773, %716 ], [ %435, %712 ]
  %779 = phi float [ %775, %716 ], [ %436, %712 ]
  %780 = fmul float %779, 0x3EE4F8B580000000
  %781 = fmul float %778, 0x3EE4F8B580000000
  %782 = fmul float %777, 0x3EE4F8B580000000
  %783 = fdiv fast float %421, %433
  %784 = fdiv fast float %425, %433
  %785 = fmul fast float %783, 5.000000e-01
  %786 = fmul fast float %784, 5.000000e-01
  %787 = fadd fast float %785, 5.000000e-01
  %788 = fsub fast float 5.000000e-01, %786
  %789 = fmul fast float %782, %782
  %790 = fmul fast float %781, %781
  %791 = fadd fast float %790, %789
  %792 = fmul fast float %780, %780
  %793 = fadd fast float %791, %792
  %794 = call float @dx.op.unary.f32(i32 24, float %793)  ; Sqrt(value)
  %795 = fsub fast float %794, %93
  %796 = call float @dx.op.binary.f32(i32 35, float 0.000000e+00, float %795)  ; FMax(a,b)
  %797 = fmul fast float %97, %95
  %798 = fmul fast float %797, %796
  %799 = call float @dx.op.unary.f32(i32 24, float %798)  ; Sqrt(value)
  %800 = fmul fast float %799, %94
  %801 = fcmp fast olt float %800, 0x3FE6A09E60000000
  br i1 %801, label %802, label %806

; <label>:802                                     ; preds = %776
  %803 = fmul fast float %800, %800
  %804 = fmul fast float %803, 2.000000e+00
  %805 = call float @dx.op.unary.f32(i32 7, float %804)  ; Saturate(value)
  br label %806

; <label>:806                                     ; preds = %802, %776
  %807 = phi float [ %805, %802 ], [ 1.000000e+00, %776 ]
  %808 = fmul fast float %796, 1.000000e+05
  %809 = call float @dx.op.unary.f32(i32 7, float %808)  ; Saturate(value)
  %810 = fmul fast float %809, %807
  br i1 %703, label %811, label %838

; <label>:811                                     ; preds = %806
  %812 = call float @dx.op.dot3.f32(i32 55, float %782, float %781, float %780, float %782, float %781, float %780)  ; Dot3(ax,ay,az,bx,by,bz)
  %813 = call float @dx.op.unary.f32(i32 25, float %812)  ; Rsqrt(value)
  %814 = fmul fast float %813, %782
  %815 = fmul fast float %813, %781
  %816 = fmul fast float %813, %780
  %817 = fmul fast float %816, %816
  %818 = fsub fast float 1.000000e+00, %817
  %819 = call float @dx.op.unary.f32(i32 24, float %818)  ; Sqrt(value)
  %820 = fdiv fast float %814, %819
  %821 = fdiv fast float %815, %819
  %822 = call float @dx.op.unary.f32(i32 15, float %820)  ; Acos(value)
  %823 = fcmp fast olt float %821, 0.000000e+00
  %824 = fsub fast float 0x401921FB60000000, %822
  %825 = select i1 %823, float %824, float %822
  %826 = fmul fast float %825, 0x3FC45F3060000000
  %827 = fmul fast float %90, 5.000000e-01
  %828 = fadd fast float %826, %827
  %829 = fadd fast float %816, %91
  %830 = fmul fast float %829, 5.000000e-01
  %831 = fadd fast float %830, 5.000000e-01
  %832 = fadd fast float %88, 1.000000e+00
  %833 = fadd fast float %89, 1.000000e+00
  %834 = fdiv fast float %88, %832
  %835 = fdiv fast float %89, %833
  %836 = fmul fast float %828, %834
  %837 = fmul fast float %835, %831
  br label %838

; <label>:838                                     ; preds = %811, %806
  %839 = phi float [ %836, %811 ], [ %787, %806 ]
  %840 = phi float [ %837, %811 ], [ %788, %806 ]
  %841 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %6, %dx.types.ResourceProperties { i32 4, i32 1033 })  ; AnnotateHandle(res,props)  resource: Texture3D<4xF32>
  %842 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %10, %dx.types.ResourceProperties { i32 14, i32 0 })  ; AnnotateHandle(res,props)  resource: SamplerState
  %843 = call %dx.types.ResRet.f32 @dx.op.sampleLevel.f32(i32 62, %dx.types.Handle %841, %dx.types.Handle %842, float %839, float %840, float %799, float undef, i32 0, i32 0, i32 0, float 0.000000e+00)  ; SampleLevel(srv,sampler,coord0,coord1,coord2,coord3,offset0,offset1,offset2,LOD)
  %844 = extractvalue %dx.types.ResRet.f32 %843, 0
  %845 = extractvalue %dx.types.ResRet.f32 %843, 1
  %846 = extractvalue %dx.types.ResRet.f32 %843, 2
  %847 = extractvalue %dx.types.ResRet.f32 %843, 3
  %848 = fsub fast float 1.000000e+00, %847
  %849 = fmul fast float %848, %810
  %850 = fsub fast float 1.000000e+00, %849
  %851 = fmul fast float %810, %84
  %852 = fmul fast float %851, %844
  %853 = fmul fast float %851, %845
  %854 = fmul fast float %851, %846
  br label %855

; <label>:855                                     ; preds = %838, %702
  %856 = phi float [ %852, %838 ], [ 0.000000e+00, %702 ]
  %857 = phi float [ %853, %838 ], [ 0.000000e+00, %702 ]
  %858 = phi float [ %854, %838 ], [ 0.000000e+00, %702 ]
  %859 = phi float [ %850, %838 ], [ 1.000000e+00, %702 ]
  %860 = fmul fast float %856, %700
  %861 = fmul fast float %857, %700
  %862 = fmul fast float %858, %700
  %863 = fadd fast float %860, %697
  %864 = fadd fast float %861, %698
  %865 = fadd fast float %862, %699
  %866 = fmul fast float %859, %700
  br label %867

; <label>:867                                     ; preds = %855, %696
  %868 = phi float [ %863, %855 ], [ %697, %696 ]
  %869 = phi float [ %864, %855 ], [ %698, %696 ]
  %870 = phi float [ %865, %855 ], [ %699, %696 ]
  %871 = phi float [ %866, %855 ], [ %700, %696 ]
  %872 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %21, i32 160)  ; CBufferLoadLegacy(handle,regIndex)
  %873 = extractvalue %dx.types.CBufRet.f32 %872, 2
  %874 = fcmp fast ogt float %873, 0.000000e+00
  br i1 %874, label %875, label %937

; <label>:875                                     ; preds = %867
  %876 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %21, i32 161)  ; CBufferLoadLegacy(handle,regIndex)
  %877 = extractvalue %dx.types.CBufRet.f32 %876, 2
  %878 = extractvalue %dx.types.CBufRet.f32 %876, 3
  %879 = extractvalue %dx.types.CBufRet.f32 %876, 0
  %880 = extractvalue %dx.types.CBufRet.f32 %876, 1
  %881 = fdiv fast float %421, %433
  %882 = fdiv fast float %425, %433
  %883 = fmul fast float %881, 5.000000e-01
  %884 = fmul fast float %882, 5.000000e-01
  %885 = fadd fast float %883, 5.000000e-01
  %886 = fsub fast float 5.000000e-01, %884
  %887 = fmul fast float %885, %879
  %888 = fmul fast float %886, %880
  %889 = call float @dx.op.binary.f32(i32 36, float %887, float %877)  ; FMin(a,b)
  %890 = call float @dx.op.binary.f32(i32 36, float %888, float %878)  ; FMin(a,b)
  %891 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %7, %dx.types.ResourceProperties { i32 2, i32 1033 })  ; AnnotateHandle(res,props)  resource: Texture2D<4xF32>
  %892 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %12, %dx.types.ResourceProperties { i32 14, i32 0 })  ; AnnotateHandle(res,props)  resource: SamplerState
  %893 = call %dx.types.ResRet.f32 @dx.op.textureGather.f32(i32 73, %dx.types.Handle %891, %dx.types.Handle %892, float %889, float %890, float undef, float undef, i32 0, i32 0, i32 0)  ; TextureGather(srv,sampler,coord0,coord1,coord2,coord3,offset0,offset1,channel)
  %894 = extractvalue %dx.types.ResRet.f32 %893, 0
  %895 = extractvalue %dx.types.ResRet.f32 %893, 1
  %896 = extractvalue %dx.types.ResRet.f32 %893, 2
  %897 = extractvalue %dx.types.ResRet.f32 %893, 3
  %898 = call float @dx.op.binary.f32(i32 36, float %896, float %897)  ; FMin(a,b)
  %899 = call float @dx.op.binary.f32(i32 36, float %894, float %895)  ; FMin(a,b)
  %900 = call float @dx.op.binary.f32(i32 36, float %899, float %898)  ; FMin(a,b)
  %901 = fmul fast float %434, %434
  %902 = fmul fast float %435, %435
  %903 = fadd fast float %902, %901
  %904 = fmul fast float %436, %436
  %905 = fadd fast float %903, %904
  %906 = call float @dx.op.unary.f32(i32 24, float %905)  ; Sqrt(value)
  %907 = fmul fast float %906, 0x3EE4F8B580000000
  %908 = fsub fast float %907, %900
  %909 = fcmp fast ogt float %908, 0.000000e+00
  br i1 %909, label %910, label %937

; <label>:910                                     ; preds = %875
  %911 = extractvalue %dx.types.CBufRet.f32 %872, 3
  %912 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %8, %dx.types.ResourceProperties { i32 2, i32 1033 })  ; AnnotateHandle(res,props)  resource: Texture2D<4xF32>
  %913 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %13, %dx.types.ResourceProperties { i32 14, i32 0 })  ; AnnotateHandle(res,props)  resource: SamplerState
  %914 = call %dx.types.ResRet.f32 @dx.op.sampleLevel.f32(i32 62, %dx.types.Handle %912, %dx.types.Handle %913, float %889, float %890, float undef, float undef, i32 0, i32 0, i32 undef, float 0.000000e+00)  ; SampleLevel(srv,sampler,coord0,coord1,coord2,coord3,offset0,offset1,offset2,LOD)
  %915 = extractvalue %dx.types.ResRet.f32 %914, 0
  %916 = extractvalue %dx.types.ResRet.f32 %914, 1
  %917 = extractvalue %dx.types.ResRet.f32 %914, 2
  %918 = extractvalue %dx.types.ResRet.f32 %914, 3
  %919 = fmul fast float %915, %84
  %920 = fmul fast float %916, %84
  %921 = fmul fast float %917, %84
  %922 = fdiv fast float %908, %911
  %923 = call float @dx.op.unary.f32(i32 7, float %922)  ; Saturate(value)
  %924 = fadd fast float %918, -1.000000e+00
  %925 = fmul fast float %919, %923
  %926 = fmul fast float %920, %923
  %927 = fmul fast float %921, %923
  %928 = fmul fast float %923, %924
  %929 = fadd fast float %928, 1.000000e+00
  %930 = fmul fast float %929, %868
  %931 = fmul fast float %929, %869
  %932 = fmul fast float %929, %870
  %933 = fadd fast float %930, %925
  %934 = fadd fast float %931, %926
  %935 = fadd fast float %932, %927
  %936 = fmul fast float %929, %871
  br label %937

; <label>:937                                     ; preds = %910, %875, %867
  %938 = phi float [ %868, %867 ], [ %933, %910 ], [ %868, %875 ]
  %939 = phi float [ %869, %867 ], [ %934, %910 ], [ %869, %875 ]
  %940 = phi float [ %870, %867 ], [ %935, %910 ], [ %870, %875 ]
  %941 = phi float [ %871, %867 ], [ %936, %910 ], [ %871, %875 ]
  call void @dx.op.storeOutput.f32(i32 5, i32 0, i32 0, i8 0, float %355)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 0, i32 0, i8 1, float %358)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 0, i32 0, i8 2, float %361)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 0, i32 0, i8 3, float 0.000000e+00)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 1, i32 0, i8 0, float %364)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 1, i32 0, i8 1, float %367)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 1, i32 0, i8 2, float %370)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 1, i32 0, i8 3, float %371)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.i32(i32 5, i32 2, i32 0, i8 0, i32 %154)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 3, i32 0, i8 0, float %938)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 3, i32 0, i8 1, float %939)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 3, i32 0, i8 2, float %940)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 3, i32 0, i8 3, float %941)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 4, i32 0, i8 0, float %390)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 4, i32 0, i8 1, float %391)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 4, i32 0, i8 2, float %392)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 5, i32 0, i8 0, float %421)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 5, i32 0, i8 1, float %425)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 5, i32 0, i8 2, float %429)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 5, i32 0, i8 3, float %433)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  ret void
}

; Function Attrs: nounwind readnone
declare float @dx.op.loadInput.f32(i32, i32, i32, i8, i32) #0

; Function Attrs: nounwind readnone
declare i32 @dx.op.loadInput.i32(i32, i32, i32, i8, i32) #0

; Function Attrs: nounwind
declare void @dx.op.storeOutput.f32(i32, i32, i32, i8, float) #1

; Function Attrs: nounwind
declare void @dx.op.storeOutput.i32(i32, i32, i32, i8, i32) #1

; Function Attrs: nounwind readonly
declare %dx.types.ResRet.f32 @dx.op.rawBufferLoad.f32(i32, %dx.types.Handle, i32, i32, i8, i32) #2

; Function Attrs: nounwind readonly
declare %dx.types.ResRet.f32 @dx.op.bufferLoad.f32(i32, %dx.types.Handle, i32, i32) #2

; Function Attrs: nounwind readonly
declare %dx.types.ResRet.i32 @dx.op.rawBufferLoad.i32(i32, %dx.types.Handle, i32, i32, i8, i32) #2

; Function Attrs: nounwind readnone
declare float @dx.op.dot3.f32(i32, float, float, float, float, float, float) #0

; Function Attrs: nounwind readnone
declare float @dx.op.unary.f32(i32, float) #0

; Function Attrs: nounwind readnone
declare float @dx.op.dot2.f32(i32, float, float, float, float) #0

; Function Attrs: nounwind readnone
declare float @dx.op.binary.f32(i32, float, float) #0

; Function Attrs: nounwind readnone
declare float @dx.op.tertiary.f32(i32, float, float, float) #0

; Function Attrs: nounwind readonly
declare %dx.types.ResRet.f32 @dx.op.sampleLevel.f32(i32, %dx.types.Handle, %dx.types.Handle, float, float, float, float, i32, i32, i32, float) #2

; Function Attrs: nounwind readonly
declare %dx.types.ResRet.f32 @dx.op.textureGather.f32(i32, %dx.types.Handle, %dx.types.Handle, float, float, float, float, i32, i32, i32) #2

; Function Attrs: nounwind readonly
declare %dx.types.CBufRet.i32 @dx.op.cbufferLoadLegacy.i32(i32, %dx.types.Handle, i32) #2

; Function Attrs: nounwind readonly
declare %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32, %dx.types.Handle, i32) #2

; Function Attrs: nounwind readnone
declare %dx.types.Handle @dx.op.annotateHandle(i32, %dx.types.Handle, %dx.types.ResourceProperties) #0

; Function Attrs: nounwind readnone
declare %dx.types.Handle @dx.op.createHandleFromBinding(i32, %dx.types.ResBind, i32, i1) #0

attributes #0 = { nounwind readnone }
attributes #1 = { nounwind }
attributes #2 = { nounwind readonly }

!llvm.ident = !{!0}
!dx.version = !{!1}
!dx.valver = !{!2}
!dx.shaderModel = !{!3}
!dx.resources = !{!4}
!dx.viewIdState = !{!28}
!dx.entryPoints = !{!29}

!0 = !{!"dxc(private) 1.8.0.0 (private, 00000000)"}
!1 = !{i32 1, i32 6}
!2 = !{i32 1, i32 8}
!3 = !{!"vs", i32 6, i32 6}
!4 = !{!5, null, !18, !23}
!5 = !{!6, !8, !9, !10, !11, !13, !15, !16, !17}
!6 = !{i32 0, %"class.TextureCube<vector<float, 4> >"* undef, !"", i32 0, i32 0, i32 1, i32 5, i32 0, !7}
!7 = !{i32 0, i32 9}
!8 = !{i32 1, %"class.Texture2D<vector<float, 4> >"* undef, !"", i32 0, i32 1, i32 1, i32 2, i32 0, !7}
!9 = !{i32 2, %"class.Texture2D<vector<float, 4> >"* undef, !"", i32 0, i32 2, i32 1, i32 2, i32 0, !7}
!10 = !{i32 3, %"class.Texture3D<vector<float, 4> >"* undef, !"", i32 0, i32 3, i32 1, i32 4, i32 0, !7}
!11 = !{i32 4, %"class.StructuredBuffer<vector<float, 4> >"* undef, !"", i32 0, i32 4, i32 1, i32 12, i32 0, !12}
!12 = !{i32 1, i32 16}
!13 = !{i32 5, %"class.StructuredBuffer<unsigned int>"* undef, !"", i32 0, i32 5, i32 1, i32 12, i32 0, !14}
!14 = !{i32 1, i32 4}
!15 = !{i32 6, %"class.StructuredBuffer<vector<float, 4> >"* undef, !"", i32 0, i32 6, i32 1, i32 12, i32 0, !12}
!16 = !{i32 7, %"class.StructuredBuffer<vector<float, 4> >"* undef, !"", i32 0, i32 7, i32 1, i32 12, i32 0, !12}
!17 = !{i32 8, %"class.Buffer<vector<float, 4> >"* undef, !"", i32 0, i32 8, i32 1, i32 10, i32 0, !7}
!18 = !{!19, !20, !21, !22}
!19 = !{i32 0, %hostlayout.TranslucentBasePass* undef, !"", i32 0, i32 0, i32 1, i32 3764, null}
!20 = !{i32 1, %hostlayout.View* undef, !"", i32 0, i32 1, i32 1, i32 10076, null}
!21 = !{i32 2, %Scene* undef, !"", i32 0, i32 2, i32 1, i32 348, null}
!22 = !{i32 3, %LocalVF* undef, !"", i32 0, i32 3, i32 1, i32 60, null}
!23 = !{!24, !25, !26, !27}
!24 = !{i32 0, %struct.SamplerState* undef, !"", i32 0, i32 0, i32 1, i32 0, null}
!25 = !{i32 1, %struct.SamplerState* undef, !"", i32 0, i32 1, i32 1, i32 0, null}
!26 = !{i32 2, %struct.SamplerState* undef, !"", i32 0, i32 2, i32 1, i32 0, null}
!27 = !{i32 3, %struct.SamplerState* undef, !"", i32 0, i32 3, i32 1, i32 0, null}
!28 = !{[15 x i32] [i32 13, i32 24, i32 16248832, i32 16248832, i32 16248832, i32 0, i32 16249335, i32 0, i32 0, i32 0, i32 16249335, i32 0, i32 0, i32 0, i32 247]}
!29 = !{void ()* @Main, !"Main", !30, !4, !50}
!30 = !{!31, !40, null}
!31 = !{!32, !35, !38, !39}
!32 = !{i32 0, !"ATTRIBUTE", i8 9, i8 0, !33, i8 0, i32 1, i8 4, i32 0, i8 0, !34}
!33 = !{i32 0}
!34 = !{i32 3, i32 7}
!35 = !{i32 1, !"ATTRIBUTE", i8 5, i8 0, !36, i8 0, i32 1, i8 1, i32 1, i8 0, !37}
!36 = !{i32 13}
!37 = !{i32 3, i32 1}
!38 = !{i32 2, !"SV_InstanceID", i8 5, i8 2, !33, i8 0, i32 1, i8 1, i32 2, i8 0, !37}
!39 = !{i32 3, !"SV_VertexID", i8 5, i8 1, !33, i8 0, i32 1, i8 1, i32 3, i8 0, !37}
!40 = !{!41, !43, !44, !45, !47, !49}
!41 = !{i32 0, !"TEXCOORD10_centroid", i8 9, i8 0, !33, i8 2, i32 1, i8 4, i32 0, i8 0, !42}
!42 = !{i32 3, i32 15}
!43 = !{i32 1, !"TEXCOORD11_centroid", i8 9, i8 0, !33, i8 2, i32 1, i8 4, i32 1, i8 0, !42}
!44 = !{i32 2, !"PRIMITIVE_ID", i8 5, i8 0, !33, i8 1, i32 1, i8 1, i32 2, i8 0, !37}
!45 = !{i32 3, !"TEXCOORD", i8 9, i8 0, !46, i8 2, i32 1, i8 4, i32 3, i8 0, !42}
!46 = !{i32 7}
!47 = !{i32 4, !"TEXCOORD", i8 9, i8 0, !48, i8 2, i32 1, i8 3, i32 4, i8 0, !34}
!48 = !{i32 9}
!49 = !{i32 5, !"SV_Position", i8 9, i8 3, !33, i8 4, i32 1, i8 4, i32 5, i8 0, !42}
!50 = !{i32 0, i64 16, i32 5, !33}
!51 = !{i32 1}
!52 = distinct !{!52, !"dx.controlflow.hints", i32 1}
!53 = distinct !{!53, !"dx.controlflow.hints", i32 1}
!54 = distinct !{!54, !"dx.controlflow.hints", i32 1}
!55 = distinct !{!55, !"dx.controlflow.hints", i32 1}
