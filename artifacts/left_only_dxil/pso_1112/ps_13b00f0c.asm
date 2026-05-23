;
; Input signature:
;
; Name                 Index   Mask Register SysValue  Format   Used
; -------------------- ----- ------ -------- -------- ------- ------
; TEXCOORD10_centroid      0   xyzw        0     NONE   float   xyz 
; TEXCOORD11_centroid      0   xyzw        1     NONE   float   xyzw
; PRIMITIVE_ID             0   x           2     NONE    uint   x   
; SV_IsFrontFace           0    y          2    FFACE    uint       
; TEXCOORD                 7   xyzw        3     NONE   float   xyzw
; TEXCOORD                 9   xyz         4     NONE   float   xyz 
; SV_Position              0   xyzw        5      POS   float   xyzw
;
;
; Output signature:
;
; Name                 Index   Mask Register SysValue  Format   Used
; -------------------- ----- ------ -------- -------- ------- ------
; SV_Target                0   xyzw        0   TARGET   float   xyzw
;
; shader debug name: b45b74a0fdbf1df81d19f9112c09d848.pdb
; shader hash: b45b74a0fdbf1df81d19f9112c09d848
;
; Pipeline Runtime Information: 
;
;PSVRuntimeInfo:
; Pixel Shader
; DepthOutput=0
; SampleFrequency=0
; MinimumExpectedWaveLaneCount: 0
; MaximumExpectedWaveLaneCount: 4294967295
; UsesViewID: false
; SigInputElements: 7
; SigOutputElements: 1
; SigPatchConstOrPrimElements: 0
; SigInputVectors: 6
; SigOutputVectors[0]: 1
; SigOutputVectors[1]: 0
; SigOutputVectors[2]: 0
; SigOutputVectors[3]: 0
; EntryFunctionName: MainPS
;
;
; Input signature:
;
; Name                 Index             InterpMode DynIdx
; -------------------- ----- ---------------------- ------
; TEXCOORD10_centroid      0                 linear       
; TEXCOORD11_centroid      0                 linear       
; PRIMITIVE_ID             0        nointerpolation       
; TEXCOORD                 7                 linear       
; TEXCOORD                 9                 linear       
; SV_Position              0          noperspective       
; SV_IsFrontFace           0        nointerpolation       
;
; Output signature:
;
; Name                 Index             InterpMode DynIdx
; -------------------- ----- ---------------------- ------
; SV_Target                0                              
;
; Buffer Definitions:
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
;   [3764 x i8] (type annotation not present)
;
; }
;
; cbuffer 
; {
;
;   [124 x i8] (type annotation not present)
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
;                                   sampler      NA          NA      S0             s0     1
;                                   texture  struct         r/o      T0             t0     1
;                                   texture     f32          3d      T1             t1     1
;                                   texture     f32          3d      T2             t2     1
;
;
; ViewId state:
;
; Number of inputs: 24, outputs: 4
; Outputs dependent on ViewId: {  }
; Inputs contributing to computation of Outputs:
;   output 0 depends on inputs: { 0, 1, 2, 4, 5, 6, 7, 8, 12, 15, 16, 17, 18, 20, 21, 22, 23 }
;   output 1 depends on inputs: { 0, 1, 2, 4, 5, 6, 7, 8, 13, 15, 20, 21, 22, 23 }
;   output 2 depends on inputs: { 0, 1, 2, 4, 5, 6, 7, 8, 14, 15, 16, 17, 18, 20, 21, 22, 23 }
;   output 3 depends on inputs: { 0, 1, 2, 4, 5, 6, 7, 8, 16, 17, 18, 20, 21, 22 }
;
target datalayout = "e-m:e-p:32:32-i1:32-i8:32-i16:32-i32:32-i64:64-f16:32-f32:32-f64:64-n8:16:32:64"
target triple = "dxil-ms-dx"

%dx.types.Handle = type { i8* }
%dx.types.ResBind = type { i32, i32, i32, i8 }
%dx.types.ResourceProperties = type { i32, i32 }
%dx.types.CBufRet.f32 = type { float, float, float, float }
%dx.types.CBufRet.i32 = type { i32, i32, i32, i32 }
%dx.types.ResRet.f32 = type { float, float, float, float, i32 }
%"class.StructuredBuffer<vector<float, 4> >" = type { <4 x float> }
%"class.Texture3D<vector<float, 4> >" = type { <4 x float>, %"class.Texture3D<vector<float, 4> >::mips_type" }
%"class.Texture3D<vector<float, 4> >::mips_type" = type { i32 }
%hostlayout.View = type { [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <4 x float>, <4 x float>, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], <4 x float>, <4 x float>, <2 x float>, <2 x float>, <2 x float>, <2 x float>, <4 x float>, <4 x float>, <4 x i32>, <4 x float>, <4 x float>, <4 x float>, <4 x float>, <2 x float>, <2 x float>, i32, float, float, float, <4 x float>, <4 x float>, <4 x float>, <2 x float>, <2 x float>, float, float, float, float, <3 x float>, float, float, float, float, float, float, float, float, i32, i32, i32, i32, i32, i32, i32, i32, float, float, float, float, float, <4 x float>, <3 x float>, float, [2 x <4 x float>], [2 x <4 x float>], <4 x float>, <4 x float>, float, float, float, float, float, float, float, float, float, float, float, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, [2 x <4 x float>], [2 x <4 x float>], [2 x <4 x float>], [2 x <4 x float>], [2 x <4 x float>], <4 x float>, <3 x float>, float, <4 x float>, [4 x <4 x float>], <4 x float>, float, float, float, float, <4 x float>, float, float, float, float, float, float, float, float, <3 x float>, float, float, float, float, float, <4 x float>, float, float, float, float, <4 x float>, float, float, float, float, [8 x <4 x float>], float, float, float, float, i32, float, float, float, <3 x float>, i32, [6 x <4 x float>], [6 x <4 x float>], [6 x <4 x float>], [6 x <4 x float>], float, float, i32, i32, <3 x float>, float, <3 x float>, float, float, float, i32, float, float, float, float, float, float, float, <2 x i32>, float, float, float, float, <3 x float>, float, <3 x float>, float, <2 x float>, <2 x float>, <2 x float>, <2 x float>, <2 x float>, float, float, <3 x float>, float, <2 x float>, <2 x float>, float, float, float, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, float, float, float, float, [2 x <4 x float>], <4 x float>, i32, i32, i32, i32, i32, i32, i32, float, <4 x float>, i32, i32, i32, i32, <4 x i32>, <2 x float>, float, float, <4 x float>, <4 x float>, <4 x float>, <3 x float>, float, i32, i32, i32, i32, [32 x <4 x i32>], i32, float, float, float, <4 x float>, <4 x float>, <4 x float>, <4 x float>, <2 x float>, float, float, <4 x float>, <4 x float>, <4 x float>, float, float, float, i32, <4 x i32>, i32, i32, i32, i32, [4 x <4 x float>], <3 x i32>, i32, <3 x float>, float, <3 x float>, float, <3 x i32>, i32, [16 x <4 x float>], float, float, float, float, float, float, float, float, [4 x [4 x <4 x float>]], [4 x <4 x float>], [4 x <4 x float>], [16 x [4 x <4 x float>]], [16 x <4 x float>], [16 x <4 x float>], [8 x [4 x <4 x float>]], [8 x <4 x float>], [8 x <4 x float>], [8 x <4 x float>], [8 x <4 x float>], [2 x <4 x float>], [2 x <4 x float>], [2 x <4 x float>], [2 x <4 x float>], [2 x <4 x float>], [2 x <4 x float>], [2 x <4 x float>], [2 x <4 x float>], [2 x <4 x float>], [2 x <4 x float>], <4 x float>, <4 x float>, float, i32, i32, float, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, <3 x float>, i32, <4 x float>, <4 x float>, <4 x float>, <3 x float>, i32, <3 x i32>, i32, <3 x i32>, i32, <3 x i32>, i32, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, <4 x float>, float, float, i32, i32, i32, i32, i32, i32, <4 x float>, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, <3 x i32>, i32, <3 x i32>, i32, <3 x float>, float, <3 x float> }
%hostlayout.TranslucentBasePass = type { i32, i32, i32, i32, i32, i32, i32, i32, <3 x i32>, i32, i32, i32, i32, i32, <3 x float>, float, <3 x float>, float, float, float, float, float, <3 x float>, float, float, float, i32, i32, <2 x float>, i32, i32, <4 x float>, [4 x [4 x <4 x float>]], [4 x <4 x float>], <4 x float>, float, i32, i32, i32, <4 x float>, [4 x <4 x float>], i32, i32, i32, i32, <4 x float>, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, float, <4 x float>, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, float, <4 x float>, <4 x float>, <4 x float>, <4 x float>, [3 x <4 x float>], <3 x float>, float, <2 x float>, float, float, [2 x [4 x <4 x float>]], [2 x <4 x float>], <2 x float>, i32, i32, i32, i32, i32, float, <4 x float>, <4 x float>, <4 x float>, <4 x float>, <4 x float>, <4 x float>, <4 x float>, <2 x float>, float, float, <3 x float>, float, float, float, float, float, <3 x float>, float, <4 x float>, <4 x float>, <4 x float>, <4 x float>, <4 x float>, <4 x float>, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, float, <4 x float>, <4 x float>, <4 x float>, <4 x float>, <4 x float>, <4 x float>, <4 x float>, <2 x float>, float, float, <3 x float>, float, float, float, float, float, <3 x float>, float, <4 x float>, <4 x float>, <4 x float>, <4 x float>, <4 x float>, <4 x float>, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, float, i32, i32, <2 x i32>, i32, i32, float, i32, float, float, float, float, <4 x float>, <3 x float>, float, <3 x float>, float, i32, float, float, float, i32, i32, i32, i32, i32, i32, float, float, i32, float, float, float, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, float, float, float, i32, i32, i32, i32, i32, i32, float, float, i32, i32, i32, i32, i32, float, float, float, [4 x <4 x float>], float, float, i32, i32, i32, i32, float, float, i32, i32, i32, i32, float, float, i32, i32, i32, float, float, float, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, <2 x float>, <2 x float>, float, float, <4 x i32>, <2 x float>, float, float, <4 x float>, <4 x float>, <2 x float>, <2 x float>, i32, i32, i32, i32, <4 x float>, <2 x float>, float, float, <4 x float>, <2 x float>, <2 x float>, float, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, float, float, <2 x float>, <2 x float>, i32, i32, i32, i32, i32, i32, i32, i32, float, float, float, float, float, float, float, float, float, float, <2 x i32>, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, float, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, [6 x <4 x float>], [6 x <4 x float>], <2 x float>, <2 x float>, <2 x float>, float, i32, i32, i32, i32, float, i32, i32, i32, i32, i32, i32, i32, float, float, float, float, float, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, <3 x float>, i32, <3 x i32>, float, i32, i32, i32, i32, i32, i32, i32, i32, i32, float, float, float, <3 x i32>, i32, <3 x i32>, i32, i32, i32, i32, i32, i32, float, float, float, [6 x [4 x <4 x float>]], <3 x float>, float, <4 x float>, <2 x i32>, i32, i32, i32, i32, float, float, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32 }
%Material = type { [7 x <4 x float>], i32, i32, i32 }
%struct.SamplerState = type { i32 }

define void @MainPS() {
  %1 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 2, i32 2, i32 0, i8 0 }, i32 2, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %2 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 1, i32 1, i32 0, i8 0 }, i32 1, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %3 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind zeroinitializer, i32 0, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %4 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 0, i32 0, i32 0, i8 3 }, i32 0, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %5 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 2, i32 2, i32 0, i8 2 }, i32 2, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %6 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 1, i32 1, i32 0, i8 2 }, i32 1, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %7 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 0, i32 0, i32 0, i8 2 }, i32 0, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %8 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %5, %dx.types.ResourceProperties { i32 13, i32 124 })  ; AnnotateHandle(res,props)  resource: CBuffer
  %9 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %6, %dx.types.ResourceProperties { i32 13, i32 3764 })  ; AnnotateHandle(res,props)  resource: CBuffer
  %10 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %7, %dx.types.ResourceProperties { i32 13, i32 10076 })  ; AnnotateHandle(res,props)  resource: CBuffer
  %11 = call float @dx.op.loadInput.f32(i32 4, i32 5, i32 0, i8 0, i32 undef), !dx.precise !37  ; LoadInput(inputSigId,rowIndex,colIndex,gsVertexAxis)
  %12 = call float @dx.op.loadInput.f32(i32 4, i32 5, i32 0, i8 1, i32 undef), !dx.precise !37  ; LoadInput(inputSigId,rowIndex,colIndex,gsVertexAxis)
  %13 = call float @dx.op.loadInput.f32(i32 4, i32 5, i32 0, i8 2, i32 undef), !dx.precise !37  ; LoadInput(inputSigId,rowIndex,colIndex,gsVertexAxis)
  %14 = call float @dx.op.loadInput.f32(i32 4, i32 5, i32 0, i8 3, i32 undef), !dx.precise !37  ; LoadInput(inputSigId,rowIndex,colIndex,gsVertexAxis)
  %15 = call float @dx.op.loadInput.f32(i32 4, i32 4, i32 0, i8 0, i32 undef), !dx.precise !37  ; LoadInput(inputSigId,rowIndex,colIndex,gsVertexAxis)
  %16 = call float @dx.op.loadInput.f32(i32 4, i32 4, i32 0, i8 1, i32 undef), !dx.precise !37  ; LoadInput(inputSigId,rowIndex,colIndex,gsVertexAxis)
  %17 = call float @dx.op.loadInput.f32(i32 4, i32 4, i32 0, i8 2, i32 undef), !dx.precise !37  ; LoadInput(inputSigId,rowIndex,colIndex,gsVertexAxis)
  %18 = call float @dx.op.loadInput.f32(i32 4, i32 3, i32 0, i8 0, i32 undef)  ; LoadInput(inputSigId,rowIndex,colIndex,gsVertexAxis)
  %19 = call float @dx.op.loadInput.f32(i32 4, i32 3, i32 0, i8 1, i32 undef)  ; LoadInput(inputSigId,rowIndex,colIndex,gsVertexAxis)
  %20 = call float @dx.op.loadInput.f32(i32 4, i32 3, i32 0, i8 2, i32 undef)  ; LoadInput(inputSigId,rowIndex,colIndex,gsVertexAxis)
  %21 = call float @dx.op.loadInput.f32(i32 4, i32 3, i32 0, i8 3, i32 undef)  ; LoadInput(inputSigId,rowIndex,colIndex,gsVertexAxis)
  %22 = call i32 @dx.op.loadInput.i32(i32 4, i32 2, i32 0, i8 0, i32 undef)  ; LoadInput(inputSigId,rowIndex,colIndex,gsVertexAxis)
  %23 = call float @dx.op.loadInput.f32(i32 4, i32 1, i32 0, i8 0, i32 undef)  ; LoadInput(inputSigId,rowIndex,colIndex,gsVertexAxis)
  %24 = call float @dx.op.loadInput.f32(i32 4, i32 1, i32 0, i8 1, i32 undef)  ; LoadInput(inputSigId,rowIndex,colIndex,gsVertexAxis)
  %25 = call float @dx.op.loadInput.f32(i32 4, i32 1, i32 0, i8 2, i32 undef)  ; LoadInput(inputSigId,rowIndex,colIndex,gsVertexAxis)
  %26 = call float @dx.op.loadInput.f32(i32 4, i32 1, i32 0, i8 3, i32 undef)  ; LoadInput(inputSigId,rowIndex,colIndex,gsVertexAxis)
  %27 = call float @dx.op.loadInput.f32(i32 4, i32 0, i32 0, i8 0, i32 undef)  ; LoadInput(inputSigId,rowIndex,colIndex,gsVertexAxis)
  %28 = call float @dx.op.loadInput.f32(i32 4, i32 0, i32 0, i8 1, i32 undef)  ; LoadInput(inputSigId,rowIndex,colIndex,gsVertexAxis)
  %29 = call float @dx.op.loadInput.f32(i32 4, i32 0, i32 0, i8 2, i32 undef)  ; LoadInput(inputSigId,rowIndex,colIndex,gsVertexAxis)
  %30 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %10, i32 4)  ; CBufferLoadLegacy(handle,regIndex)
  %31 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %10, i32 5)  ; CBufferLoadLegacy(handle,regIndex)
  %32 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %10, i32 6)  ; CBufferLoadLegacy(handle,regIndex)
  %33 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %10, i32 7)  ; CBufferLoadLegacy(handle,regIndex)
  %34 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %10, i32 31)  ; CBufferLoadLegacy(handle,regIndex)
  %35 = extractvalue %dx.types.CBufRet.f32 %34, 3
  %36 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %10, i32 44)  ; CBufferLoadLegacy(handle,regIndex)
  %37 = extractvalue %dx.types.CBufRet.f32 %36, 0
  %38 = extractvalue %dx.types.CBufRet.f32 %36, 1
  %39 = extractvalue %dx.types.CBufRet.f32 %36, 2
  %40 = extractvalue %dx.types.CBufRet.f32 %36, 3
  %41 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %10, i32 45)  ; CBufferLoadLegacy(handle,regIndex)
  %42 = extractvalue %dx.types.CBufRet.f32 %41, 0
  %43 = extractvalue %dx.types.CBufRet.f32 %41, 1
  %44 = extractvalue %dx.types.CBufRet.f32 %41, 2
  %45 = extractvalue %dx.types.CBufRet.f32 %41, 3
  %46 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %10, i32 46)  ; CBufferLoadLegacy(handle,regIndex)
  %47 = extractvalue %dx.types.CBufRet.f32 %46, 0
  %48 = extractvalue %dx.types.CBufRet.f32 %46, 1
  %49 = extractvalue %dx.types.CBufRet.f32 %46, 2
  %50 = extractvalue %dx.types.CBufRet.f32 %46, 3
  %51 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %10, i32 47)  ; CBufferLoadLegacy(handle,regIndex)
  %52 = extractvalue %dx.types.CBufRet.f32 %51, 0
  %53 = extractvalue %dx.types.CBufRet.f32 %51, 1
  %54 = extractvalue %dx.types.CBufRet.f32 %51, 2
  %55 = extractvalue %dx.types.CBufRet.f32 %51, 3
  %56 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %10, i32 72)  ; CBufferLoadLegacy(handle,regIndex)
  %57 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %10, i32 73)  ; CBufferLoadLegacy(handle,regIndex)
  %58 = extractvalue %dx.types.CBufRet.f32 %57, 0
  %59 = extractvalue %dx.types.CBufRet.f32 %57, 1
  %60 = extractvalue %dx.types.CBufRet.f32 %57, 2
  %61 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %10, i32 121)  ; CBufferLoadLegacy(handle,regIndex)
  %62 = extractvalue %dx.types.CBufRet.f32 %61, 0
  %63 = extractvalue %dx.types.CBufRet.f32 %61, 1
  %64 = extractvalue %dx.types.CBufRet.f32 %61, 2
  %65 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %10, i32 124)  ; CBufferLoadLegacy(handle,regIndex)
  %66 = extractvalue %dx.types.CBufRet.f32 %65, 0
  %67 = extractvalue %dx.types.CBufRet.f32 %65, 1
  %68 = extractvalue %dx.types.CBufRet.f32 %65, 2
  %69 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %10, i32 156)  ; CBufferLoadLegacy(handle,regIndex)
  %70 = extractvalue %dx.types.CBufRet.f32 %69, 3
  %71 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %10, i32 159)  ; CBufferLoadLegacy(handle,regIndex)
  %72 = extractvalue %dx.types.CBufRet.f32 %71, 0
  %73 = extractvalue %dx.types.CBufRet.f32 %71, 1
  %74 = extractvalue %dx.types.CBufRet.f32 %71, 2
  %75 = extractvalue %dx.types.CBufRet.f32 %71, 3
  %76 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %10, i32 253)  ; CBufferLoadLegacy(handle,regIndex)
  %77 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %10, i32 258)  ; CBufferLoadLegacy(handle,regIndex)
  %78 = fsub float -0.000000e+00, %62
  %79 = fsub float -0.000000e+00, %63
  %80 = fsub float -0.000000e+00, %64
  %81 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %10, i32 182)  ; CBufferLoadLegacy(handle,regIndex)
  %82 = extractvalue %dx.types.CBufRet.f32 %81, 0
  %83 = extractvalue %dx.types.CBufRet.f32 %81, 1
  %84 = extractvalue %dx.types.CBufRet.f32 %81, 2
  %85 = fmul fast float %29, %24
  %86 = fmul fast float %28, %25
  %87 = fsub fast float %85, %86
  %88 = fmul fast float %27, %25
  %89 = fmul fast float %29, %23
  %90 = fsub fast float %88, %89
  %91 = fmul fast float %28, %23
  %92 = fmul fast float %27, %24
  %93 = fsub fast float %91, %92
  %94 = fmul fast float %87, %26
  %95 = fmul fast float %90, %26
  %96 = fmul fast float %93, %26
  %97 = fmul float %11, %37
  %98 = call float @dx.op.tertiary.f32(i32 46, float %12, float %42, float %97), !dx.precise !37  ; FMad(a,b,c)
  %99 = call float @dx.op.tertiary.f32(i32 46, float %13, float %47, float %98), !dx.precise !37  ; FMad(a,b,c)
  %100 = call float @dx.op.tertiary.f32(i32 46, float 1.000000e+00, float %52, float %99), !dx.precise !37  ; FMad(a,b,c)
  %101 = fmul float %11, %38
  %102 = call float @dx.op.tertiary.f32(i32 46, float %12, float %43, float %101), !dx.precise !37  ; FMad(a,b,c)
  %103 = call float @dx.op.tertiary.f32(i32 46, float %13, float %48, float %102), !dx.precise !37  ; FMad(a,b,c)
  %104 = call float @dx.op.tertiary.f32(i32 46, float 1.000000e+00, float %53, float %103), !dx.precise !37  ; FMad(a,b,c)
  %105 = fmul float %11, %39
  %106 = call float @dx.op.tertiary.f32(i32 46, float %12, float %44, float %105), !dx.precise !37  ; FMad(a,b,c)
  %107 = call float @dx.op.tertiary.f32(i32 46, float %13, float %49, float %106), !dx.precise !37  ; FMad(a,b,c)
  %108 = call float @dx.op.tertiary.f32(i32 46, float 1.000000e+00, float %54, float %107), !dx.precise !37  ; FMad(a,b,c)
  %109 = fmul float %11, %40
  %110 = call float @dx.op.tertiary.f32(i32 46, float %12, float %45, float %109), !dx.precise !37  ; FMad(a,b,c)
  %111 = call float @dx.op.tertiary.f32(i32 46, float %13, float %50, float %110), !dx.precise !37  ; FMad(a,b,c)
  %112 = call float @dx.op.tertiary.f32(i32 46, float 1.000000e+00, float %55, float %111), !dx.precise !37  ; FMad(a,b,c)
  %113 = fdiv float %100, %112
  %114 = fdiv float %104, %112
  %115 = fdiv float %108, %112
  %116 = fsub float %113, %66
  %117 = fsub float %114, %67
  %118 = fsub float %115, %68
  %119 = call float @dx.op.tertiary.f32(i32 46, float %62, float 2.097152e+06, float %116), !dx.precise !37  ; FMad(a,b,c)
  %120 = call float @dx.op.tertiary.f32(i32 46, float %63, float 2.097152e+06, float %117), !dx.precise !37  ; FMad(a,b,c)
  %121 = call float @dx.op.tertiary.f32(i32 46, float %64, float 2.097152e+06, float %118), !dx.precise !37  ; FMad(a,b,c)
  %122 = call float @dx.op.tertiary.f32(i32 46, float %78, float 2.097152e+06, float %119), !dx.precise !37  ; FMad(a,b,c)
  %123 = call float @dx.op.tertiary.f32(i32 46, float %79, float 2.097152e+06, float %120), !dx.precise !37  ; FMad(a,b,c)
  %124 = call float @dx.op.tertiary.f32(i32 46, float %80, float 2.097152e+06, float %121), !dx.precise !37  ; FMad(a,b,c)
  %125 = fsub float %116, %122
  %126 = fsub float %117, %123
  %127 = fsub float %118, %124
  %128 = fsub fast float -0.000000e+00, %113
  %129 = fsub fast float -0.000000e+00, %114
  %130 = fsub fast float -0.000000e+00, %115
  %131 = call float @dx.op.dot3.f32(i32 55, float %128, float %129, float %130, float %128, float %129, float %130)  ; Dot3(ax,ay,az,bx,by,bz)
  %132 = call float @dx.op.unary.f32(i32 25, float %131)  ; Rsqrt(value)
  %133 = fmul fast float %132, %128
  %134 = fmul fast float %132, %129
  %135 = fmul fast float %132, %130
  %136 = fsub fast float -0.000000e+00, %58
  %137 = fsub fast float -0.000000e+00, %59
  %138 = fsub fast float -0.000000e+00, %60
  %139 = fcmp fast oge float %35, 1.000000e+00
  %140 = select i1 %139, float %136, float %133
  %141 = select i1 %139, float %137, float %134
  %142 = select i1 %139, float %138, float %135
  %143 = fadd fast float %75, %74
  %144 = call float @dx.op.dot3.f32(i32 55, float %72, float %73, float %143, float %72, float %73, float %143)  ; Dot3(ax,ay,az,bx,by,bz)
  %145 = call float @dx.op.unary.f32(i32 25, float %144)  ; Rsqrt(value)
  %146 = fmul fast float %145, %72
  %147 = fmul fast float %145, %73
  %148 = fmul fast float %145, %143
  %149 = fmul fast float %146, %27
  %150 = call float @dx.op.tertiary.f32(i32 46, float %147, float %94, float %149)  ; FMad(a,b,c)
  %151 = call float @dx.op.tertiary.f32(i32 46, float %148, float %23, float %150)  ; FMad(a,b,c)
  %152 = fmul fast float %146, %28
  %153 = call float @dx.op.tertiary.f32(i32 46, float %147, float %95, float %152)  ; FMad(a,b,c)
  %154 = call float @dx.op.tertiary.f32(i32 46, float %148, float %24, float %153)  ; FMad(a,b,c)
  %155 = fmul fast float %146, %29
  %156 = call float @dx.op.tertiary.f32(i32 46, float %147, float %96, float %155)  ; FMad(a,b,c)
  %157 = call float @dx.op.tertiary.f32(i32 46, float %148, float %25, float %156)  ; FMad(a,b,c)
  %158 = call float @dx.op.dot3.f32(i32 55, float %151, float %154, float %157, float %151, float %154, float %157)  ; Dot3(ax,ay,az,bx,by,bz)
  %159 = call float @dx.op.unary.f32(i32 25, float %158)  ; Rsqrt(value)
  %160 = fmul fast float %159, %151
  %161 = fmul fast float %159, %154
  %162 = fmul fast float %159, %157
  %163 = call float @dx.op.dot3.f32(i32 55, float %160, float %161, float %162, float %140, float %141, float %142)  ; Dot3(ax,ay,az,bx,by,bz)
  %164 = call float @dx.op.binary.f32(i32 35, float 0.000000e+00, float %163)  ; FMax(a,b)
  %165 = fsub fast float 1.000000e+00, %164
  %166 = call float @dx.op.unary.f32(i32 6, float %165)  ; FAbs(value)
  %167 = call float @dx.op.binary.f32(i32 35, float %166, float 0x3F1A36E2E0000000)  ; FMax(a,b)
  %168 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %8, i32 0)  ; CBufferLoadLegacy(handle,regIndex)
  %169 = extractvalue %dx.types.CBufRet.f32 %168, 0
  %170 = fcmp fast ole float %167, 0x3E60000040000000
  %171 = call float @dx.op.unary.f32(i32 23, float %167)  ; Log(value)
  %172 = fmul fast float %171, %169
  %173 = call float @dx.op.unary.f32(i32 21, float %172)  ; Exp(value)
  %174 = select i1 %170, float 0.000000e+00, float %173
  %175 = extractvalue %dx.types.CBufRet.f32 %168, 1
  %176 = fmul fast float %175, %174
  %177 = extractvalue %dx.types.CBufRet.f32 %168, 2
  %178 = fadd fast float %176, %177
  %179 = extractvalue %dx.types.CBufRet.f32 %168, 3
  %180 = fcmp fast ole float %178, 0x3E60000040000000
  %181 = call float @dx.op.unary.f32(i32 23, float %178)  ; Log(value)
  %182 = fmul fast float %181, %179
  %183 = call float @dx.op.unary.f32(i32 21, float %182)  ; Exp(value)
  %184 = select i1 %180, float 0.000000e+00, float %183
  %185 = call float @dx.op.unary.f32(i32 7, float %184)  ; Saturate(value)
  %186 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %8, i32 1)  ; CBufferLoadLegacy(handle,regIndex)
  %187 = extractvalue %dx.types.CBufRet.f32 %186, 0
  %188 = fmul fast float %171, %187
  %189 = call float @dx.op.unary.f32(i32 21, float %188)  ; Exp(value)
  %190 = select i1 %170, float 0.000000e+00, float %189
  %191 = extractvalue %dx.types.CBufRet.f32 %186, 1
  %192 = fmul fast float %191, %190
  %193 = extractvalue %dx.types.CBufRet.f32 %186, 2
  %194 = fsub fast float 1.000000e+00, %193
  %195 = fsub fast float %194, %192
  %196 = extractvalue %dx.types.CBufRet.f32 %186, 3
  %197 = fcmp fast ole float %195, 0x3E60000040000000
  %198 = call float @dx.op.unary.f32(i32 23, float %195)  ; Log(value)
  %199 = fmul fast float %198, %196
  %200 = call float @dx.op.unary.f32(i32 21, float %199)  ; Exp(value)
  %201 = select i1 %197, float 0.000000e+00, float %200
  %202 = call float @dx.op.unary.f32(i32 7, float %201)  ; Saturate(value)
  %203 = fmul fast float %202, %185
  %204 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %8, i32 2)  ; CBufferLoadLegacy(handle,regIndex)
  %205 = extractvalue %dx.types.CBufRet.f32 %204, 0
  %206 = fmul fast float %205, %203
  %207 = extractvalue %dx.types.CBufRet.f32 %204, 1
  %208 = extractvalue %dx.types.CBufRet.f32 %204, 2
  %209 = extractvalue %dx.types.CBufRet.f32 %204, 3
  %210 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %8, i32 3)  ; CBufferLoadLegacy(handle,regIndex)
  %211 = extractvalue %dx.types.CBufRet.f32 %210, 0
  %212 = extractvalue %dx.types.CBufRet.f32 %210, 1
  %213 = extractvalue %dx.types.CBufRet.f32 %210, 2
  %214 = fsub fast float %207, %211
  %215 = fsub fast float %208, %212
  %216 = fsub fast float %209, %213
  %217 = fmul fast float %214, %206
  %218 = fmul fast float %215, %206
  %219 = fmul fast float %216, %206
  %220 = fadd fast float %217, %211
  %221 = fadd fast float %218, %212
  %222 = fadd fast float %219, %213
  %223 = extractvalue %dx.types.CBufRet.f32 %210, 3
  %224 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %8, i32 4)  ; CBufferLoadLegacy(handle,regIndex)
  %225 = extractvalue %dx.types.CBufRet.f32 %224, 0
  %226 = call float @dx.op.dot3.f32(i32 55, float %23, float %24, float %25, float %82, float %83, float %84)  ; Dot3(ax,ay,az,bx,by,bz)
  %227 = fsub fast float 1.000000e+00, %226
  %228 = extractvalue %dx.types.CBufRet.f32 %224, 1
  %229 = extractvalue %dx.types.CBufRet.f32 %224, 2
  %230 = fsub fast float %228, %229
  %231 = fmul fast float %230, %227
  %232 = fadd fast float %231, %229
  %233 = call float @dx.op.unary.f32(i32 7, float %232)  ; Saturate(value)
  %234 = call float @dx.op.unary.f32(i32 7, float %233)  ; Saturate(value)
  %235 = fsub fast float %225, %223
  %236 = fmul fast float %234, %235
  %237 = fadd fast float %236, %223
  %238 = fmul fast float %237, %220
  %239 = fmul fast float %237, %221
  %240 = fmul fast float %237, %222
  %241 = extractvalue %dx.types.CBufRet.f32 %224, 3
  %242 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %8, i32 5)  ; CBufferLoadLegacy(handle,regIndex)
  %243 = extractvalue %dx.types.CBufRet.f32 %242, 0
  %244 = extractvalue %dx.types.CBufRet.f32 %242, 1
  %245 = extractvalue %dx.types.CBufRet.f32 %242, 2
  %246 = fsub fast float %243, %238
  %247 = fsub fast float %244, %239
  %248 = fsub fast float %245, %240
  %249 = fmul fast float %246, %241
  %250 = fmul fast float %247, %241
  %251 = fmul fast float %248, %241
  %252 = fadd fast float %249, %238
  %253 = fadd fast float %250, %239
  %254 = fadd fast float %251, %240
  %255 = extractvalue %dx.types.CBufRet.f32 %242, 3
  %256 = fmul fast float %255, %203
  %257 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %8, i32 6)  ; CBufferLoadLegacy(handle,regIndex)
  %258 = extractvalue %dx.types.CBufRet.f32 %257, 0
  %259 = fmul fast float %256, %258
  %260 = fsub fast float %259, %256
  %261 = fmul fast float %260, %234
  %262 = fadd fast float %261, %256
  %263 = call float @dx.op.unary.f32(i32 7, float %262)  ; Saturate(value)
  %264 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %9, i32 80)  ; CBufferLoadLegacy(handle,regIndex)
  %265 = extractvalue %dx.types.CBufRet.f32 %264, 0
  %266 = fcmp ogt float %265, 0.000000e+00
  br i1 %266, label %267, label %397

; <label>:267                                     ; preds = %0
  %268 = call %dx.types.CBufRet.i32 @dx.op.cbufferLoadLegacy.i32(i32 59, %dx.types.Handle %10, i32 321)  ; CBufferLoadLegacy(handle,regIndex)
  %269 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %10, i32 252)  ; CBufferLoadLegacy(handle,regIndex)
  %270 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %10, i32 204)  ; CBufferLoadLegacy(handle,regIndex)
  %271 = extractvalue %dx.types.CBufRet.i32 %268, 0
  %272 = extractvalue %dx.types.CBufRet.f32 %77, 3
  %273 = extractvalue %dx.types.CBufRet.f32 %77, 2
  %274 = extractvalue %dx.types.CBufRet.f32 %77, 1
  %275 = extractvalue %dx.types.CBufRet.f32 %77, 0
  %276 = extractvalue %dx.types.CBufRet.f32 %76, 2
  %277 = extractvalue %dx.types.CBufRet.f32 %76, 1
  %278 = extractvalue %dx.types.CBufRet.f32 %76, 0
  %279 = extractvalue %dx.types.CBufRet.f32 %269, 2
  %280 = extractvalue %dx.types.CBufRet.f32 %270, 3
  %281 = extractvalue %dx.types.CBufRet.f32 %56, 2
  %282 = extractvalue %dx.types.CBufRet.f32 %56, 1
  %283 = extractvalue %dx.types.CBufRet.f32 %56, 0
  %284 = extractvalue %dx.types.CBufRet.f32 %33, 3
  %285 = extractvalue %dx.types.CBufRet.f32 %33, 1
  %286 = extractvalue %dx.types.CBufRet.f32 %33, 0
  %287 = extractvalue %dx.types.CBufRet.f32 %32, 3
  %288 = extractvalue %dx.types.CBufRet.f32 %32, 1
  %289 = extractvalue %dx.types.CBufRet.f32 %32, 0
  %290 = extractvalue %dx.types.CBufRet.f32 %31, 3
  %291 = extractvalue %dx.types.CBufRet.f32 %31, 1
  %292 = extractvalue %dx.types.CBufRet.f32 %31, 0
  %293 = extractvalue %dx.types.CBufRet.f32 %30, 3
  %294 = extractvalue %dx.types.CBufRet.f32 %30, 1
  %295 = extractvalue %dx.types.CBufRet.f32 %30, 0
  %296 = fsub float -0.000000e+00, %283
  %297 = fsub float -0.000000e+00, %282
  %298 = fsub float -0.000000e+00, %281
  %299 = fsub float %119, %283
  %300 = fsub float %120, %282
  %301 = fsub float %121, %281
  %302 = fsub float %299, %119
  %303 = fsub float %300, %120
  %304 = fsub float %301, %121
  %305 = fsub float %299, %302
  %306 = fsub float %300, %303
  %307 = fsub float %301, %304
  %308 = fsub float %119, %305
  %309 = fsub float %120, %306
  %310 = fsub float %121, %307
  %311 = fsub float %296, %302
  %312 = fsub float %297, %303
  %313 = fsub float %298, %304
  %314 = fadd float %311, %308
  %315 = fadd float %312, %309
  %316 = fadd float %313, %310
  %317 = fadd fast float %125, %299
  %318 = fadd fast float %317, %314
  %319 = fadd fast float %126, %300
  %320 = fadd fast float %319, %315
  %321 = fadd fast float %127, %301
  %322 = fadd fast float %321, %316
  %323 = fmul fast float %318, %295
  %324 = call float @dx.op.tertiary.f32(i32 46, float %320, float %292, float %323)  ; FMad(a,b,c)
  %325 = call float @dx.op.tertiary.f32(i32 46, float %322, float %289, float %324)  ; FMad(a,b,c)
  %326 = fadd fast float %286, %325
  %327 = fmul fast float %318, %294
  %328 = call float @dx.op.tertiary.f32(i32 46, float %320, float %291, float %327)  ; FMad(a,b,c)
  %329 = call float @dx.op.tertiary.f32(i32 46, float %322, float %288, float %328)  ; FMad(a,b,c)
  %330 = fadd fast float %285, %329
  %331 = fmul fast float %318, %293
  %332 = call float @dx.op.tertiary.f32(i32 46, float %320, float %290, float %331)  ; FMad(a,b,c)
  %333 = call float @dx.op.tertiary.f32(i32 46, float %322, float %287, float %332)  ; FMad(a,b,c)
  %334 = fadd fast float %284, %333
  %335 = fdiv fast float %326, %334
  %336 = fdiv fast float %330, %334
  %337 = fmul fast float %335, 5.000000e-01
  %338 = fmul fast float %336, 5.000000e-01
  %339 = fadd fast float %337, 5.000000e-01
  %340 = fsub fast float 5.000000e-01, %338
  %341 = fmul fast float %334, %278
  %342 = fadd fast float %341, %277
  %343 = call float @dx.op.unary.f32(i32 23, float %342)  ; Log(value)
  %344 = fmul fast float %279, %276
  %345 = fmul fast float %344, %343
  %346 = call float @dx.op.binary.f32(i32 35, float %345, float 0.000000e+00)  ; FMax(a,b)
  %347 = fmul fast float %339, %275
  %348 = fmul fast float %340, %274
  %349 = call float @dx.op.binary.f32(i32 36, float %347, float %273)  ; FMin(a,b)
  %350 = call float @dx.op.binary.f32(i32 36, float %348, float %272)  ; FMin(a,b)
  %351 = call float @dx.op.binary.f32(i32 36, float %346, float 1.000000e+00)  ; FMin(a,b)
  %352 = fcmp fast oeq float %280, 0.000000e+00
  %353 = and i32 %271, 32
  %354 = icmp eq i32 %353, 0
  %355 = and i1 %354, %352
  br i1 %355, label %397, label %356

; <label>:356                                     ; preds = %267
  %357 = extractvalue %dx.types.CBufRet.f32 %264, 1
  %358 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %2, %dx.types.ResourceProperties { i32 4, i32 1033 })  ; AnnotateHandle(res,props)  resource: Texture3D<4xF32>
  %359 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %4, %dx.types.ResourceProperties { i32 14, i32 0 })  ; AnnotateHandle(res,props)  resource: SamplerState
  %360 = call %dx.types.ResRet.f32 @dx.op.sampleLevel.f32(i32 62, %dx.types.Handle %358, %dx.types.Handle %359, float %349, float %350, float %351, float undef, i32 0, i32 0, i32 0, float 0.000000e+00)  ; SampleLevel(srv,sampler,coord0,coord1,coord2,coord3,offset0,offset1,offset2,LOD)
  %361 = extractvalue %dx.types.ResRet.f32 %360, 0
  %362 = extractvalue %dx.types.ResRet.f32 %360, 1
  %363 = extractvalue %dx.types.ResRet.f32 %360, 2
  %364 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %1, %dx.types.ResourceProperties { i32 4, i32 1033 })  ; AnnotateHandle(res,props)  resource: Texture3D<4xF32>
  %365 = call %dx.types.ResRet.f32 @dx.op.sampleLevel.f32(i32 62, %dx.types.Handle %364, %dx.types.Handle %359, float %349, float %350, float %351, float undef, i32 0, i32 0, i32 0, float 0.000000e+00)  ; SampleLevel(srv,sampler,coord0,coord1,coord2,coord3,offset0,offset1,offset2,LOD)
  %366 = extractvalue %dx.types.ResRet.f32 %365, 0
  %367 = extractvalue %dx.types.ResRet.f32 %365, 1
  %368 = extractvalue %dx.types.ResRet.f32 %365, 2
  %369 = fmul fast float %361, %70
  %370 = fmul fast float %362, %70
  %371 = fmul fast float %363, %70
  %372 = fadd float %366, -1.000000e+00
  %373 = fadd float %367, -1.000000e+00
  %374 = fadd float %368, -1.000000e+00
  %375 = fsub fast float %14, %357
  %376 = extractvalue %dx.types.CBufRet.f32 %264, 2
  %377 = fmul fast float %376, %375
  %378 = call float @dx.op.unary.f32(i32 7, float %377)  ; Saturate(value)
  %379 = fmul fast float %378, %369
  %380 = fmul fast float %378, %370
  %381 = fmul fast float %378, %371
  %382 = fmul fast float %378, %372
  %383 = fmul fast float %378, %373
  %384 = fmul fast float %378, %374
  %385 = fadd fast float %382, 1.000000e+00
  %386 = fadd fast float %383, 1.000000e+00
  %387 = fadd fast float %384, 1.000000e+00
  %388 = fmul fast float %385, %21
  %389 = fmul fast float %386, %21
  %390 = fmul fast float %387, %21
  %391 = fmul fast float %385, %18
  %392 = fmul fast float %386, %19
  %393 = fmul fast float %387, %20
  %394 = fadd fast float %391, %379
  %395 = fadd fast float %392, %380
  %396 = fadd fast float %393, %381
  br label %397

; <label>:397                                     ; preds = %356, %267, %0
  %398 = phi float [ %18, %0 ], [ %394, %356 ], [ 0.000000e+00, %267 ]
  %399 = phi float [ %19, %0 ], [ %395, %356 ], [ 0.000000e+00, %267 ]
  %400 = phi float [ %20, %0 ], [ %396, %356 ], [ 0.000000e+00, %267 ]
  %401 = phi float [ %21, %0 ], [ %388, %356 ], [ 1.000000e+00, %267 ]
  %402 = phi float [ %21, %0 ], [ %389, %356 ], [ 1.000000e+00, %267 ]
  %403 = phi float [ %21, %0 ], [ %390, %356 ], [ 1.000000e+00, %267 ]
  %404 = call float @dx.op.binary.f32(i32 35, float %252, float 0.000000e+00)  ; FMax(a,b)
  %405 = call float @dx.op.binary.f32(i32 35, float %253, float 0.000000e+00)  ; FMax(a,b)
  %406 = call float @dx.op.binary.f32(i32 35, float %254, float 0.000000e+00)  ; FMax(a,b)
  %407 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %10, i32 161)  ; CBufferLoadLegacy(handle,regIndex)
  %408 = extractvalue %dx.types.CBufRet.f32 %407, 2
  %409 = fcmp ogt float %408, 0.000000e+00
  br i1 %409, label %410, label %516, !dx.controlflow.hints !38

; <label>:410                                     ; preds = %397
  %411 = mul i32 %22, 44
  %412 = add i32 %411, 18
  %413 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %3, %dx.types.ResourceProperties { i32 12, i32 16 })  ; AnnotateHandle(res,props)  resource: StructuredBuffer<stride=16>
  %414 = call %dx.types.ResRet.f32 @dx.op.rawBufferLoad.f32(i32 139, %dx.types.Handle %413, i32 %412, i32 0, i8 15, i32 4)  ; RawBufferLoad(srv,index,elementOffset,mask,alignment)
  %415 = extractvalue %dx.types.ResRet.f32 %414, 0
  %416 = extractvalue %dx.types.ResRet.f32 %414, 1
  %417 = extractvalue %dx.types.ResRet.f32 %414, 2
  %418 = add i32 %411, 19
  %419 = call %dx.types.ResRet.f32 @dx.op.rawBufferLoad.f32(i32 139, %dx.types.Handle %413, i32 %418, i32 0, i8 15, i32 4)  ; RawBufferLoad(srv,index,elementOffset,mask,alignment)
  %420 = extractvalue %dx.types.ResRet.f32 %419, 0
  %421 = extractvalue %dx.types.ResRet.f32 %419, 1
  %422 = extractvalue %dx.types.ResRet.f32 %419, 2
  %423 = add i32 %411, 17
  %424 = call %dx.types.ResRet.f32 @dx.op.rawBufferLoad.f32(i32 139, %dx.types.Handle %413, i32 %423, i32 0, i8 15, i32 4)  ; RawBufferLoad(srv,index,elementOffset,mask,alignment)
  %425 = extractvalue %dx.types.ResRet.f32 %424, 3
  %426 = fmul float %415, 2.097152e+06
  %427 = fmul float %416, 2.097152e+06
  %428 = fmul float %417, 2.097152e+06
  %429 = fadd float %426, %420
  %430 = fadd float %427, %421
  %431 = fadd float %428, %422
  %432 = fsub float %429, %426
  %433 = fsub float %430, %427
  %434 = fsub float %431, %428
  %435 = fsub float %420, %432
  %436 = fsub float %421, %433
  %437 = fsub float %422, %434
  %438 = add i32 %411, 26
  %439 = call %dx.types.ResRet.f32 @dx.op.rawBufferLoad.f32(i32 139, %dx.types.Handle %413, i32 %438, i32 0, i8 15, i32 4)  ; RawBufferLoad(srv,index,elementOffset,mask,alignment)
  %440 = extractvalue %dx.types.ResRet.f32 %439, 3
  %441 = add i32 %411, 27
  %442 = call %dx.types.ResRet.f32 @dx.op.rawBufferLoad.f32(i32 139, %dx.types.Handle %413, i32 %441, i32 0, i8 15, i32 4)  ; RawBufferLoad(srv,index,elementOffset,mask,alignment)
  %443 = extractvalue %dx.types.ResRet.f32 %442, 3
  %444 = add i32 %411, 32
  %445 = call %dx.types.ResRet.f32 @dx.op.rawBufferLoad.f32(i32 139, %dx.types.Handle %413, i32 %444, i32 0, i8 15, i32 4)  ; RawBufferLoad(srv,index,elementOffset,mask,alignment)
  %446 = extractvalue %dx.types.ResRet.f32 %445, 0
  %447 = fsub float %119, %429
  %448 = fsub float %120, %430
  %449 = fsub float %121, %431
  %450 = fsub float %125, %435
  %451 = fsub float %126, %436
  %452 = fsub float %127, %437
  %453 = fadd float %447, %450
  %454 = fadd float %448, %451
  %455 = fadd float %449, %452
  %456 = call float @dx.op.unary.f32(i32 6, float %453)  ; FAbs(value)
  %457 = call float @dx.op.unary.f32(i32 6, float %454)  ; FAbs(value)
  %458 = call float @dx.op.unary.f32(i32 6, float %455)  ; FAbs(value)
  %459 = fadd fast float %425, 1.000000e+00
  %460 = fadd fast float %440, 1.000000e+00
  %461 = fadd fast float %443, 1.000000e+00
  %462 = fcmp fast ogt float %456, %459
  %463 = fcmp fast ogt float %457, %460
  %464 = fcmp fast ogt float %458, %461
  %465 = or i1 %462, %463
  %466 = or i1 %465, %464
  br i1 %466, label %467, label %494

; <label>:467                                     ; preds = %410
  %468 = fmul fast float %125, 0x3EF0000000000000
  %469 = fmul fast float %126, 0x3EF0000000000000
  %470 = fmul fast float %127, 0x3EF0000000000000
  %471 = fmul fast float %119, 0x3EF0000000000000
  %472 = fmul fast float %120, 0x3EF0000000000000
  %473 = fmul fast float %121, 0x3EF0000000000000
  %474 = call float @dx.op.unary.f32(i32 22, float %471)  ; Frc(value)
  %475 = call float @dx.op.unary.f32(i32 22, float %472)  ; Frc(value)
  %476 = call float @dx.op.unary.f32(i32 22, float %473)  ; Frc(value)
  %477 = call float @dx.op.unary.f32(i32 22, float %468)  ; Frc(value)
  %478 = call float @dx.op.unary.f32(i32 22, float %469)  ; Frc(value)
  %479 = call float @dx.op.unary.f32(i32 22, float %470)  ; Frc(value)
  %480 = fadd fast float %477, %474
  %481 = fadd fast float %478, %475
  %482 = fadd fast float %479, %476
  %483 = call float @dx.op.unary.f32(i32 22, float %480)  ; Frc(value)
  %484 = call float @dx.op.unary.f32(i32 22, float %481)  ; Frc(value)
  %485 = call float @dx.op.unary.f32(i32 22, float %482)  ; Frc(value)
  %486 = fmul fast float %483, 6.553600e+04
  %487 = fmul fast float %484, 6.553600e+04
  %488 = fmul fast float %485, 6.553600e+04
  %489 = call float @dx.op.dot3.f32(i32 55, float %486, float %487, float %488, float 0x3F52E83A20000000, float 0x3F52E83A20000000, float 0x3F52E83A20000000)  ; Dot3(ax,ay,az,bx,by,bz)
  %490 = call float @dx.op.unary.f32(i32 22, float %489)  ; Frc(value)
  %491 = fcmp fast ogt float %490, 5.000000e-01
  %492 = uitofp i1 %491 to float
  %493 = fsub fast float 1.000000e+00, %492
  br label %516

; <label>:494                                     ; preds = %410
  %495 = fcmp fast ogt float %446, 0.000000e+00
  br i1 %495, label %496, label %516

; <label>:496                                     ; preds = %494
  %497 = fsub fast float %113, %15
  %498 = fsub fast float %114, %16
  %499 = fsub fast float %115, %17
  %500 = call float @dx.op.unary.f32(i32 6, float %497)  ; FAbs(value)
  %501 = call float @dx.op.unary.f32(i32 6, float %498)  ; FAbs(value)
  %502 = call float @dx.op.unary.f32(i32 6, float %499)  ; FAbs(value)
  %503 = call float @dx.op.binary.f32(i32 35, float %501, float %502)  ; FMax(a,b)
  %504 = call float @dx.op.binary.f32(i32 35, float %500, float %503)  ; FMax(a,b)
  %505 = fsub fast float %504, %446
  %506 = call float @dx.op.unary.f32(i32 6, float %505)  ; FAbs(value)
  %507 = fmul fast float %506, 2.000000e+01
  %508 = call float @dx.op.unary.f32(i32 7, float %507)  ; Saturate(value)
  %509 = fsub fast float 1.000000e+00, %508
  %510 = fcmp fast ogt float %509, 0.000000e+00
  %511 = fcmp fast olt float %509, 0.000000e+00
  %512 = zext i1 %510 to i32
  %513 = zext i1 %511 to i32
  %514 = sub nsw i32 %512, %513
  %515 = sitofp i32 %514 to float
  br label %516

; <label>:516                                     ; preds = %496, %494, %467, %397
  %517 = phi float [ 1.000000e+00, %467 ], [ %515, %496 ], [ %263, %494 ], [ %263, %397 ]
  %518 = phi float [ %493, %467 ], [ %509, %496 ], [ %404, %494 ], [ %404, %397 ]
  %519 = phi float [ 1.000000e+00, %467 ], [ 0.000000e+00, %496 ], [ %405, %494 ], [ %405, %397 ]
  %520 = phi float [ %492, %467 ], [ %509, %496 ], [ %406, %494 ], [ %406, %397 ]
  %521 = fmul fast float %518, %401
  %522 = fmul fast float %519, %402
  %523 = fmul fast float %520, %403
  %524 = fadd fast float %521, %398
  %525 = fadd fast float %522, %399
  %526 = fadd fast float %523, %400
  %527 = extractvalue %dx.types.CBufRet.f32 %69, 2
  %528 = fmul fast float %527, %524
  %529 = fmul fast float %527, %525
  %530 = fmul fast float %526, %527
  %531 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %10, i32 320)  ; CBufferLoadLegacy(handle,regIndex)
  %532 = extractvalue %dx.types.CBufRet.f32 %531, 2
  %533 = call float @dx.op.binary.f32(i32 36, float %528, float %532)  ; FMin(a,b)
  %534 = call float @dx.op.binary.f32(i32 36, float %529, float %532)  ; FMin(a,b)
  %535 = call float @dx.op.binary.f32(i32 36, float %530, float %532)  ; FMin(a,b)
  call void @dx.op.storeOutput.f32(i32 5, i32 0, i32 0, i8 0, float %533)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 0, i32 0, i8 1, float %534)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 0, i32 0, i8 2, float %535)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 0, i32 0, i8 3, float %517)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  ret void
}

; Function Attrs: nounwind readnone
declare float @dx.op.loadInput.f32(i32, i32, i32, i8, i32) #0

; Function Attrs: nounwind readnone
declare i32 @dx.op.loadInput.i32(i32, i32, i32, i8, i32) #0

; Function Attrs: nounwind
declare void @dx.op.storeOutput.f32(i32, i32, i32, i8, float) #1

; Function Attrs: nounwind readnone
declare float @dx.op.unary.f32(i32, float) #0

; Function Attrs: nounwind readnone
declare float @dx.op.dot3.f32(i32, float, float, float, float, float, float) #0

; Function Attrs: nounwind readnone
declare float @dx.op.tertiary.f32(i32, float, float, float) #0

; Function Attrs: nounwind readnone
declare float @dx.op.binary.f32(i32, float, float) #0

; Function Attrs: nounwind readonly
declare %dx.types.ResRet.f32 @dx.op.rawBufferLoad.f32(i32, %dx.types.Handle, i32, i32, i8, i32) #2

; Function Attrs: nounwind readonly
declare %dx.types.ResRet.f32 @dx.op.sampleLevel.f32(i32, %dx.types.Handle, %dx.types.Handle, float, float, float, float, i32, i32, i32, float) #2

; Function Attrs: nounwind readonly
declare %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32, %dx.types.Handle, i32) #2

; Function Attrs: nounwind readonly
declare %dx.types.CBufRet.i32 @dx.op.cbufferLoadLegacy.i32(i32, %dx.types.Handle, i32) #2

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
!dx.viewIdState = !{!17}
!dx.entryPoints = !{!18}

!0 = !{!"dxc(private) 1.8.0.0 (private, 00000000)"}
!1 = !{i32 1, i32 6}
!2 = !{i32 1, i32 8}
!3 = !{!"ps", i32 6, i32 6}
!4 = !{!5, null, !11, !15}
!5 = !{!6, !8, !10}
!6 = !{i32 0, %"class.StructuredBuffer<vector<float, 4> >"* undef, !"", i32 0, i32 0, i32 1, i32 12, i32 0, !7}
!7 = !{i32 1, i32 16}
!8 = !{i32 1, %"class.Texture3D<vector<float, 4> >"* undef, !"", i32 0, i32 1, i32 1, i32 4, i32 0, !9}
!9 = !{i32 0, i32 9}
!10 = !{i32 2, %"class.Texture3D<vector<float, 4> >"* undef, !"", i32 0, i32 2, i32 1, i32 4, i32 0, !9}
!11 = !{!12, !13, !14}
!12 = !{i32 0, %hostlayout.View* undef, !"", i32 0, i32 0, i32 1, i32 10076, null}
!13 = !{i32 1, %hostlayout.TranslucentBasePass* undef, !"", i32 0, i32 1, i32 1, i32 3764, null}
!14 = !{i32 2, %Material* undef, !"", i32 0, i32 2, i32 1, i32 124, null}
!15 = !{!16}
!16 = !{i32 0, %struct.SamplerState* undef, !"", i32 0, i32 0, i32 1, i32 0, null}
!17 = !{[26 x i32] [i32 24, i32 4, i32 15, i32 15, i32 15, i32 0, i32 15, i32 15, i32 15, i32 15, i32 15, i32 0, i32 0, i32 0, i32 1, i32 2, i32 4, i32 7, i32 13, i32 13, i32 13, i32 0, i32 15, i32 15, i32 15, i32 7]}
!18 = !{void ()* @MainPS, !"MainPS", !19, !4, !36}
!19 = !{!20, !34, null}
!20 = !{!21, !24, !26, !28, !30, !32, !33}
!21 = !{i32 0, !"TEXCOORD10_centroid", i8 9, i8 0, !22, i8 2, i32 1, i8 4, i32 0, i8 0, !23}
!22 = !{i32 0}
!23 = !{i32 3, i32 7}
!24 = !{i32 1, !"TEXCOORD11_centroid", i8 9, i8 0, !22, i8 2, i32 1, i8 4, i32 1, i8 0, !25}
!25 = !{i32 3, i32 15}
!26 = !{i32 2, !"PRIMITIVE_ID", i8 5, i8 0, !22, i8 1, i32 1, i8 1, i32 2, i8 0, !27}
!27 = !{i32 3, i32 1}
!28 = !{i32 3, !"TEXCOORD", i8 9, i8 0, !29, i8 2, i32 1, i8 4, i32 3, i8 0, !25}
!29 = !{i32 7}
!30 = !{i32 4, !"TEXCOORD", i8 9, i8 0, !31, i8 2, i32 1, i8 3, i32 4, i8 0, !23}
!31 = !{i32 9}
!32 = !{i32 5, !"SV_Position", i8 9, i8 3, !22, i8 4, i32 1, i8 4, i32 5, i8 0, !25}
!33 = !{i32 6, !"SV_IsFrontFace", i8 5, i8 13, !22, i8 1, i32 1, i8 1, i32 2, i8 1, null}
!34 = !{!35}
!35 = !{i32 0, !"SV_Target", i8 9, i8 16, !22, i8 0, i32 1, i8 4, i32 0, i8 0, !25}
!36 = !{i32 0, i64 16, i32 5, !22}
!37 = !{i32 1}
!38 = distinct !{!38, !"dx.controlflow.hints", i32 1}
