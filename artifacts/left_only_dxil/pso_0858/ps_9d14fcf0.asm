;
; Input signature:
;
; Name                 Index   Mask Register SysValue  Format   Used
; -------------------- ----- ------ -------- -------- ------- ------
; TEXCOORD10_centroid      0   xyzw        0     NONE   float       
; TEXCOORD11_centroid      0   xyzw        1     NONE   float       
; PRIMITIVE_ID             0   x           2     NONE    uint   x   
; SV_RenderTargetArrayIndex     0    y          2  RTINDEX    uint       
; SV_Position              0   xyzw        3      POS   float   xyzw
;
;
; Output signature:
;
; Name                 Index   Mask Register SysValue  Format   Used
; -------------------- ----- ------ -------- -------- ------- ------
; SV_Target                0   xyzw        0   TARGET   float   xyzw
; SV_Target                1   xyzw        1   TARGET   float   xyzw
; SV_Target                2   xyzw        2   TARGET   float   xyzw
;
; shader debug name: 73c45865c287a6d5e1b3d06b633b9300.pdb
; shader hash: 73c45865c287a6d5e1b3d06b633b9300
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
; SigInputElements: 5
; SigOutputElements: 3
; SigPatchConstOrPrimElements: 0
; SigInputVectors: 4
; SigOutputVectors[0]: 3
; SigOutputVectors[1]: 0
; SigOutputVectors[2]: 0
; SigOutputVectors[3]: 0
; EntryFunctionName: VoxelizePS
;
;
; Input signature:
;
; Name                 Index             InterpMode DynIdx
; -------------------- ----- ---------------------- ------
; TEXCOORD10_centroid      0                 linear       
; TEXCOORD11_centroid      0                 linear       
; PRIMITIVE_ID             0        nointerpolation       
; SV_Position              0          noperspective       
; SV_RenderTargetArrayIndex     0        nointerpolation       
;
; Output signature:
;
; Name                 Index             InterpMode DynIdx
; -------------------- ----- ---------------------- ------
; SV_Target                0                              
; SV_Target                1                              
; SV_Target                2                              
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
;   [392 x i8] (type annotation not present)
;
; }
;
; cbuffer 
; {
;
;   [108 x i8] (type annotation not present)
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
;                                   texture     f32         buf      T0             t0     1
;                                   texture     f32         buf      T1             t1     1
;                                   texture  struct         r/o      T2             t2     1
;
;
; ViewId state:
;
; Number of inputs: 16, outputs: 12
; Outputs dependent on ViewId: {  }
; Inputs contributing to computation of Outputs:
;   output 0 depends on inputs: { 12, 13, 14, 15 }
;   output 1 depends on inputs: { 12, 13, 14, 15 }
;   output 2 depends on inputs: { 12, 13, 14, 15 }
;   output 3 depends on inputs: { 12, 13, 14 }
;   output 4 depends on inputs: { 12, 13, 14, 15 }
;   output 5 depends on inputs: { 12, 13, 14, 15 }
;   output 6 depends on inputs: { 12, 13, 14, 15 }
;   output 7 depends on inputs: { 12, 13, 14 }
;   output 11 depends on inputs: { 12, 13, 14 }
;
target datalayout = "e-m:e-p:32:32-i1:32-i8:32-i16:32-i32:32-i64:64-f16:32-f32:32-f64:64-n8:16:32:64"
target triple = "dxil-ms-dx"

%dx.types.Handle = type { i8* }
%dx.types.ResBind = type { i32, i32, i32, i8 }
%dx.types.ResourceProperties = type { i32, i32 }
%dx.types.CBufRet.f32 = type { float, float, float, float }
%dx.types.ResRet.f32 = type { float, float, float, float, i32 }
%dx.types.CBufRet.i32 = type { i32, i32, i32, i32 }
%"class.Buffer<vector<float, 4> >" = type { <4 x float> }
%"class.StructuredBuffer<vector<float, 4> >" = type { <4 x float> }
%hostlayout.View = type { [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <4 x float>, <4 x float>, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], <4 x float>, <4 x float>, <2 x float>, <2 x float>, <2 x float>, <2 x float>, <4 x float>, <4 x float>, <4 x i32>, <4 x float>, <4 x float>, <4 x float>, <4 x float>, <2 x float>, <2 x float>, i32, float, float, float, <4 x float>, <4 x float>, <4 x float>, <2 x float>, <2 x float>, float, float, float, float, <3 x float>, float, float, float, float, float, float, float, float, i32, i32, i32, i32, i32, i32, i32, i32, float, float, float, float, float, <4 x float>, <3 x float>, float, [2 x <4 x float>], [2 x <4 x float>], <4 x float>, <4 x float>, float, float, float, float, float, float, float, float, float, float, float, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, [2 x <4 x float>], [2 x <4 x float>], [2 x <4 x float>], [2 x <4 x float>], [2 x <4 x float>], <4 x float>, <3 x float>, float, <4 x float>, [4 x <4 x float>], <4 x float>, float, float, float, float, <4 x float>, float, float, float, float, float, float, float, float, <3 x float>, float, float, float, float, float, <4 x float>, float, float, float, float, <4 x float>, float, float, float, float, [8 x <4 x float>], float, float, float, float, i32, float, float, float, <3 x float>, i32, [6 x <4 x float>], [6 x <4 x float>], [6 x <4 x float>], [6 x <4 x float>], float, float, i32, i32, <3 x float>, float, <3 x float>, float, float, float, i32, float, float, float, float, float, float, float, <2 x i32>, float, float, float, float, <3 x float>, float, <3 x float>, float, <2 x float>, <2 x float>, <2 x float>, <2 x float>, <2 x float>, float, float, <3 x float>, float, <2 x float>, <2 x float>, float, float, float, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, float, float, float, float, [2 x <4 x float>], <4 x float>, i32, i32, i32, i32, i32, i32, i32, float, <4 x float>, i32, i32, i32, i32, <4 x i32>, <2 x float>, float, float, <4 x float>, <4 x float>, <4 x float>, <3 x float>, float, i32, i32, i32, i32, [32 x <4 x i32>], i32, float, float, float, <4 x float>, <4 x float>, <4 x float>, <4 x float>, <2 x float>, float, float, <4 x float>, <4 x float>, <4 x float>, float, float, float, i32, <4 x i32>, i32, i32, i32, i32, [4 x <4 x float>], <3 x i32>, i32, <3 x float>, float, <3 x float>, float, <3 x i32>, i32, [16 x <4 x float>], float, float, float, float, float, float, float, float, [4 x [4 x <4 x float>]], [4 x <4 x float>], [4 x <4 x float>], [16 x [4 x <4 x float>]], [16 x <4 x float>], [16 x <4 x float>], [8 x [4 x <4 x float>]], [8 x <4 x float>], [8 x <4 x float>], [8 x <4 x float>], [8 x <4 x float>], [2 x <4 x float>], [2 x <4 x float>], [2 x <4 x float>], [2 x <4 x float>], [2 x <4 x float>], [2 x <4 x float>], [2 x <4 x float>], [2 x <4 x float>], [2 x <4 x float>], [2 x <4 x float>], <4 x float>, <4 x float>, float, i32, i32, float, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, <3 x float>, i32, <4 x float>, <4 x float>, <4 x float>, <3 x float>, i32, <3 x i32>, i32, <3 x i32>, i32, <3 x i32>, i32, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, <4 x float>, float, float, i32, i32, i32, i32, i32, i32, <4 x float>, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, <3 x i32>, i32, <3 x i32>, i32, <3 x float>, float, <3 x float> }
%hostlayout.VoxelizeVolumePass = type { i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, float, float, float, [4 x <4 x float>], <2 x float>, float, float, <4 x float>, <3 x i32>, i32, <3 x float>, float, <3 x i32>, i32, <3 x float>, float, <3 x float>, float, <2 x float>, float, float, <3 x float>, float, <3 x float>, float, <2 x i32>, float, float, <3 x float>, float, float, float }
%Material = type { [6 x <4 x float>], i32, i32, i32 }

define void @VoxelizePS() {
  %1 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 2, i32 2, i32 0, i8 0 }, i32 2, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %2 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 1, i32 1, i32 0, i8 0 }, i32 1, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %3 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind zeroinitializer, i32 0, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %4 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 2, i32 2, i32 0, i8 2 }, i32 2, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %5 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 1, i32 1, i32 0, i8 2 }, i32 1, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %6 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 0, i32 0, i32 0, i8 2 }, i32 0, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %7 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %4, %dx.types.ResourceProperties { i32 13, i32 108 })  ; AnnotateHandle(res,props)  resource: CBuffer
  %8 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %5, %dx.types.ResourceProperties { i32 13, i32 392 })  ; AnnotateHandle(res,props)  resource: CBuffer
  %9 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %6, %dx.types.ResourceProperties { i32 13, i32 10076 })  ; AnnotateHandle(res,props)  resource: CBuffer
  %10 = call float @dx.op.loadInput.f32(i32 4, i32 3, i32 0, i8 0, i32 undef), !dx.precise !30  ; LoadInput(inputSigId,rowIndex,colIndex,gsVertexAxis)
  %11 = call float @dx.op.loadInput.f32(i32 4, i32 3, i32 0, i8 1, i32 undef), !dx.precise !30  ; LoadInput(inputSigId,rowIndex,colIndex,gsVertexAxis)
  %12 = call float @dx.op.loadInput.f32(i32 4, i32 3, i32 0, i8 2, i32 undef), !dx.precise !30  ; LoadInput(inputSigId,rowIndex,colIndex,gsVertexAxis)
  %13 = call float @dx.op.loadInput.f32(i32 4, i32 3, i32 0, i8 3, i32 undef), !dx.precise !30  ; LoadInput(inputSigId,rowIndex,colIndex,gsVertexAxis)
  %14 = call i32 @dx.op.loadInput.i32(i32 4, i32 2, i32 0, i8 0, i32 undef)  ; LoadInput(inputSigId,rowIndex,colIndex,gsVertexAxis)
  %15 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %9, i32 44)  ; CBufferLoadLegacy(handle,regIndex)
  %16 = extractvalue %dx.types.CBufRet.f32 %15, 0
  %17 = extractvalue %dx.types.CBufRet.f32 %15, 1
  %18 = extractvalue %dx.types.CBufRet.f32 %15, 2
  %19 = extractvalue %dx.types.CBufRet.f32 %15, 3
  %20 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %9, i32 45)  ; CBufferLoadLegacy(handle,regIndex)
  %21 = extractvalue %dx.types.CBufRet.f32 %20, 0
  %22 = extractvalue %dx.types.CBufRet.f32 %20, 1
  %23 = extractvalue %dx.types.CBufRet.f32 %20, 2
  %24 = extractvalue %dx.types.CBufRet.f32 %20, 3
  %25 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %9, i32 46)  ; CBufferLoadLegacy(handle,regIndex)
  %26 = extractvalue %dx.types.CBufRet.f32 %25, 0
  %27 = extractvalue %dx.types.CBufRet.f32 %25, 1
  %28 = extractvalue %dx.types.CBufRet.f32 %25, 2
  %29 = extractvalue %dx.types.CBufRet.f32 %25, 3
  %30 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %9, i32 47)  ; CBufferLoadLegacy(handle,regIndex)
  %31 = extractvalue %dx.types.CBufRet.f32 %30, 0
  %32 = extractvalue %dx.types.CBufRet.f32 %30, 1
  %33 = extractvalue %dx.types.CBufRet.f32 %30, 2
  %34 = extractvalue %dx.types.CBufRet.f32 %30, 3
  %35 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %9, i32 84)  ; CBufferLoadLegacy(handle,regIndex)
  %36 = extractvalue %dx.types.CBufRet.f32 %35, 0
  %37 = extractvalue %dx.types.CBufRet.f32 %35, 1
  %38 = extractvalue %dx.types.CBufRet.f32 %35, 2
  %39 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %9, i32 85)  ; CBufferLoadLegacy(handle,regIndex)
  %40 = extractvalue %dx.types.CBufRet.f32 %39, 0
  %41 = extractvalue %dx.types.CBufRet.f32 %39, 1
  %42 = extractvalue %dx.types.CBufRet.f32 %39, 2
  %43 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %9, i32 121)  ; CBufferLoadLegacy(handle,regIndex)
  %44 = extractvalue %dx.types.CBufRet.f32 %43, 0
  %45 = extractvalue %dx.types.CBufRet.f32 %43, 1
  %46 = extractvalue %dx.types.CBufRet.f32 %43, 2
  %47 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %9, i32 122)  ; CBufferLoadLegacy(handle,regIndex)
  %48 = extractvalue %dx.types.CBufRet.f32 %47, 0
  %49 = extractvalue %dx.types.CBufRet.f32 %47, 1
  %50 = extractvalue %dx.types.CBufRet.f32 %47, 2
  %51 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %9, i32 124)  ; CBufferLoadLegacy(handle,regIndex)
  %52 = extractvalue %dx.types.CBufRet.f32 %51, 0
  %53 = extractvalue %dx.types.CBufRet.f32 %51, 1
  %54 = extractvalue %dx.types.CBufRet.f32 %51, 2
  %55 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %9, i32 157)  ; CBufferLoadLegacy(handle,regIndex)
  %56 = extractvalue %dx.types.CBufRet.f32 %55, 0
  %57 = extractvalue %dx.types.CBufRet.f32 %55, 1
  %58 = extractvalue %dx.types.CBufRet.f32 %55, 2
  %59 = extractvalue %dx.types.CBufRet.f32 %55, 3
  %60 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %8, i32 12)  ; CBufferLoadLegacy(handle,regIndex)
  %61 = extractvalue %dx.types.CBufRet.f32 %60, 0
  %62 = extractvalue %dx.types.CBufRet.f32 %60, 1
  %63 = fmul float %10, %61
  %64 = fmul float %11, %62
  %65 = fmul float %16, %63
  %66 = call float @dx.op.tertiary.f32(i32 46, float %64, float %21, float %65), !dx.precise !30  ; FMad(a,b,c)
  %67 = call float @dx.op.tertiary.f32(i32 46, float %12, float %26, float %66), !dx.precise !30  ; FMad(a,b,c)
  %68 = call float @dx.op.tertiary.f32(i32 46, float 1.000000e+00, float %31, float %67), !dx.precise !30  ; FMad(a,b,c)
  %69 = fmul float %17, %63
  %70 = call float @dx.op.tertiary.f32(i32 46, float %64, float %22, float %69), !dx.precise !30  ; FMad(a,b,c)
  %71 = call float @dx.op.tertiary.f32(i32 46, float %12, float %27, float %70), !dx.precise !30  ; FMad(a,b,c)
  %72 = call float @dx.op.tertiary.f32(i32 46, float 1.000000e+00, float %32, float %71), !dx.precise !30  ; FMad(a,b,c)
  %73 = fmul float %18, %63
  %74 = call float @dx.op.tertiary.f32(i32 46, float %64, float %23, float %73), !dx.precise !30  ; FMad(a,b,c)
  %75 = call float @dx.op.tertiary.f32(i32 46, float %12, float %28, float %74), !dx.precise !30  ; FMad(a,b,c)
  %76 = call float @dx.op.tertiary.f32(i32 46, float 1.000000e+00, float %33, float %75), !dx.precise !30  ; FMad(a,b,c)
  %77 = fmul float %19, %63
  %78 = call float @dx.op.tertiary.f32(i32 46, float %64, float %24, float %77), !dx.precise !30  ; FMad(a,b,c)
  %79 = call float @dx.op.tertiary.f32(i32 46, float %12, float %29, float %78), !dx.precise !30  ; FMad(a,b,c)
  %80 = call float @dx.op.tertiary.f32(i32 46, float 1.000000e+00, float %34, float %79), !dx.precise !30  ; FMad(a,b,c)
  %81 = fdiv float %68, %80
  %82 = fdiv float %72, %80
  %83 = fdiv float %76, %80
  %84 = fsub float %81, %52
  %85 = fsub float %82, %53
  %86 = fsub float %83, %54
  %87 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %7, i32 0)  ; CBufferLoadLegacy(handle,regIndex)
  %88 = extractvalue %dx.types.CBufRet.f32 %87, 0
  %89 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %9, i32 163)  ; CBufferLoadLegacy(handle,regIndex)
  %90 = extractvalue %dx.types.CBufRet.f32 %89, 2
  %91 = fptosi float %88 to i32
  %92 = fadd float %81, %36
  %93 = fadd float %82, %37
  %94 = fadd float %83, %38
  %95 = fsub float %92, %81
  %96 = fsub float %93, %82
  %97 = fsub float %94, %83
  %98 = fsub float %92, %95
  %99 = fsub float %93, %96
  %100 = fsub float %94, %97
  %101 = fsub float %81, %98
  %102 = fsub float %82, %99
  %103 = fsub float %83, %100
  %104 = fsub float %36, %95
  %105 = fsub float %37, %96
  %106 = fsub float %38, %97
  %107 = fadd float %104, %101
  %108 = fadd float %105, %102
  %109 = fadd float %106, %103
  %110 = fadd float %40, 0.000000e+00
  %111 = fadd float %41, 0.000000e+00
  %112 = fadd float %42, 0.000000e+00
  %113 = fsub float %110, %110
  %114 = fsub float %111, %111
  %115 = fsub float %112, %112
  %116 = fsub float 0.000000e+00, %113
  %117 = fsub float 0.000000e+00, %114
  %118 = fsub float 0.000000e+00, %115
  %119 = fsub float %40, %110
  %120 = fsub float %41, %111
  %121 = fsub float %42, %112
  %122 = fadd float %119, %116
  %123 = fadd float %120, %117
  %124 = fadd float %121, %118
  %125 = fadd float %110, %107
  %126 = fadd float %111, %108
  %127 = fadd float %112, %109
  %128 = fadd float %92, %125
  %129 = fadd float %93, %126
  %130 = fadd float %94, %127
  %131 = fsub float %128, %92
  %132 = fsub float %129, %93
  %133 = fsub float %130, %94
  %134 = fsub float %125, %131
  %135 = fsub float %126, %132
  %136 = fsub float %127, %133
  %137 = fadd float %122, %134
  %138 = fadd float %123, %135
  %139 = fadd float %124, %136
  %140 = fadd float %128, %137
  %141 = fadd float %129, %138
  %142 = fadd float %130, %139
  %143 = fsub float %140, %128
  %144 = fsub float %141, %129
  %145 = fsub float %142, %130
  %146 = fsub float %137, %143
  %147 = fsub float %138, %144
  %148 = fsub float %139, %145
  %149 = fadd float %140, %146
  %150 = fadd float %141, %147
  %151 = fadd float %142, %148
  %152 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %9, i32 349)  ; CBufferLoadLegacy(handle,regIndex)
  %153 = extractvalue %dx.types.CBufRet.f32 %152, 0
  %154 = extractvalue %dx.types.CBufRet.f32 %152, 1
  %155 = extractvalue %dx.types.CBufRet.f32 %152, 3
  %156 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %9, i32 350)  ; CBufferLoadLegacy(handle,regIndex)
  %157 = extractvalue %dx.types.CBufRet.f32 %156, 0
  %158 = extractvalue %dx.types.CBufRet.f32 %156, 1
  %159 = extractvalue %dx.types.CBufRet.f32 %156, 3
  %160 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %9, i32 351)  ; CBufferLoadLegacy(handle,regIndex)
  %161 = extractvalue %dx.types.CBufRet.f32 %160, 0
  %162 = extractvalue %dx.types.CBufRet.f32 %160, 1
  %163 = extractvalue %dx.types.CBufRet.f32 %160, 3
  %164 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %9, i32 352)  ; CBufferLoadLegacy(handle,regIndex)
  %165 = extractvalue %dx.types.CBufRet.f32 %164, 0
  %166 = extractvalue %dx.types.CBufRet.f32 %164, 1
  %167 = extractvalue %dx.types.CBufRet.f32 %164, 3
  %168 = fmul float %153, %149
  %169 = call float @dx.op.tertiary.f32(i32 46, float %150, float %157, float %168), !dx.precise !30  ; FMad(a,b,c)
  %170 = call float @dx.op.tertiary.f32(i32 46, float %151, float %161, float %169), !dx.precise !30  ; FMad(a,b,c)
  %171 = call float @dx.op.tertiary.f32(i32 46, float 1.000000e+00, float %165, float %170), !dx.precise !30  ; FMad(a,b,c)
  %172 = fmul float %154, %149
  %173 = call float @dx.op.tertiary.f32(i32 46, float %150, float %158, float %172), !dx.precise !30  ; FMad(a,b,c)
  %174 = call float @dx.op.tertiary.f32(i32 46, float %151, float %162, float %173), !dx.precise !30  ; FMad(a,b,c)
  %175 = call float @dx.op.tertiary.f32(i32 46, float 1.000000e+00, float %166, float %174), !dx.precise !30  ; FMad(a,b,c)
  %176 = fmul float %155, %149
  %177 = call float @dx.op.tertiary.f32(i32 46, float %150, float %159, float %176), !dx.precise !30  ; FMad(a,b,c)
  %178 = call float @dx.op.tertiary.f32(i32 46, float %151, float %163, float %177), !dx.precise !30  ; FMad(a,b,c)
  %179 = call float @dx.op.tertiary.f32(i32 46, float 1.000000e+00, float %167, float %178), !dx.precise !30  ; FMad(a,b,c)
  %180 = fdiv float %171, %179
  %181 = fdiv float %175, %179
  %182 = call float @dx.op.unary.f32(i32 6, float %180), !dx.precise !30  ; FAbs(value)
  %183 = call float @dx.op.unary.f32(i32 6, float %181), !dx.precise !30  ; FAbs(value)
  %184 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %9, i32 365)  ; CBufferLoadLegacy(handle,regIndex)
  %185 = extractvalue %dx.types.CBufRet.f32 %184, 0
  %186 = extractvalue %dx.types.CBufRet.f32 %184, 1
  %187 = fcmp ole float %182, %185
  %188 = fcmp ole float %183, %186
  %189 = and i1 %187, %188
  %190 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %9, i32 369)  ; CBufferLoadLegacy(handle,regIndex)
  %191 = extractvalue %dx.types.CBufRet.f32 %190, 0
  %192 = fcmp ogt float %191, 0xC7EFFFFFE0000000
  %193 = and i1 %189, %192
  br i1 %193, label %324, label %194

; <label>:194                                     ; preds = %0
  %195 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %9, i32 353)  ; CBufferLoadLegacy(handle,regIndex)
  %196 = extractvalue %dx.types.CBufRet.f32 %195, 0
  %197 = extractvalue %dx.types.CBufRet.f32 %195, 1
  %198 = extractvalue %dx.types.CBufRet.f32 %195, 3
  %199 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %9, i32 354)  ; CBufferLoadLegacy(handle,regIndex)
  %200 = extractvalue %dx.types.CBufRet.f32 %199, 0
  %201 = extractvalue %dx.types.CBufRet.f32 %199, 1
  %202 = extractvalue %dx.types.CBufRet.f32 %199, 3
  %203 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %9, i32 355)  ; CBufferLoadLegacy(handle,regIndex)
  %204 = extractvalue %dx.types.CBufRet.f32 %203, 0
  %205 = extractvalue %dx.types.CBufRet.f32 %203, 1
  %206 = extractvalue %dx.types.CBufRet.f32 %203, 3
  %207 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %9, i32 356)  ; CBufferLoadLegacy(handle,regIndex)
  %208 = extractvalue %dx.types.CBufRet.f32 %207, 0
  %209 = extractvalue %dx.types.CBufRet.f32 %207, 1
  %210 = extractvalue %dx.types.CBufRet.f32 %207, 3
  %211 = fmul float %149, %196
  %212 = call float @dx.op.tertiary.f32(i32 46, float %150, float %200, float %211), !dx.precise !30  ; FMad(a,b,c)
  %213 = call float @dx.op.tertiary.f32(i32 46, float %151, float %204, float %212), !dx.precise !30  ; FMad(a,b,c)
  %214 = call float @dx.op.tertiary.f32(i32 46, float 1.000000e+00, float %208, float %213), !dx.precise !30  ; FMad(a,b,c)
  %215 = fmul float %149, %197
  %216 = call float @dx.op.tertiary.f32(i32 46, float %150, float %201, float %215), !dx.precise !30  ; FMad(a,b,c)
  %217 = call float @dx.op.tertiary.f32(i32 46, float %151, float %205, float %216), !dx.precise !30  ; FMad(a,b,c)
  %218 = call float @dx.op.tertiary.f32(i32 46, float 1.000000e+00, float %209, float %217), !dx.precise !30  ; FMad(a,b,c)
  %219 = fmul float %149, %198
  %220 = call float @dx.op.tertiary.f32(i32 46, float %150, float %202, float %219), !dx.precise !30  ; FMad(a,b,c)
  %221 = call float @dx.op.tertiary.f32(i32 46, float %151, float %206, float %220), !dx.precise !30  ; FMad(a,b,c)
  %222 = call float @dx.op.tertiary.f32(i32 46, float 1.000000e+00, float %210, float %221), !dx.precise !30  ; FMad(a,b,c)
  %223 = fdiv float %214, %222
  %224 = fdiv float %218, %222
  %225 = call float @dx.op.unary.f32(i32 6, float %223), !dx.precise !30  ; FAbs(value)
  %226 = call float @dx.op.unary.f32(i32 6, float %224), !dx.precise !30  ; FAbs(value)
  %227 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %9, i32 366)  ; CBufferLoadLegacy(handle,regIndex)
  %228 = extractvalue %dx.types.CBufRet.f32 %227, 0
  %229 = extractvalue %dx.types.CBufRet.f32 %227, 1
  %230 = fcmp ole float %225, %228
  %231 = fcmp ole float %226, %229
  %232 = and i1 %230, %231
  %233 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %9, i32 370)  ; CBufferLoadLegacy(handle,regIndex)
  %234 = extractvalue %dx.types.CBufRet.f32 %233, 0
  %235 = fcmp ogt float %234, 0xC7EFFFFFE0000000
  %236 = and i1 %232, %235
  br i1 %236, label %324, label %237

; <label>:237                                     ; preds = %194
  %238 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %9, i32 357)  ; CBufferLoadLegacy(handle,regIndex)
  %239 = extractvalue %dx.types.CBufRet.f32 %238, 0
  %240 = extractvalue %dx.types.CBufRet.f32 %238, 1
  %241 = extractvalue %dx.types.CBufRet.f32 %238, 3
  %242 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %9, i32 358)  ; CBufferLoadLegacy(handle,regIndex)
  %243 = extractvalue %dx.types.CBufRet.f32 %242, 0
  %244 = extractvalue %dx.types.CBufRet.f32 %242, 1
  %245 = extractvalue %dx.types.CBufRet.f32 %242, 3
  %246 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %9, i32 359)  ; CBufferLoadLegacy(handle,regIndex)
  %247 = extractvalue %dx.types.CBufRet.f32 %246, 0
  %248 = extractvalue %dx.types.CBufRet.f32 %246, 1
  %249 = extractvalue %dx.types.CBufRet.f32 %246, 3
  %250 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %9, i32 360)  ; CBufferLoadLegacy(handle,regIndex)
  %251 = extractvalue %dx.types.CBufRet.f32 %250, 0
  %252 = extractvalue %dx.types.CBufRet.f32 %250, 1
  %253 = extractvalue %dx.types.CBufRet.f32 %250, 3
  %254 = fmul float %149, %239
  %255 = call float @dx.op.tertiary.f32(i32 46, float %150, float %243, float %254), !dx.precise !30  ; FMad(a,b,c)
  %256 = call float @dx.op.tertiary.f32(i32 46, float %151, float %247, float %255), !dx.precise !30  ; FMad(a,b,c)
  %257 = call float @dx.op.tertiary.f32(i32 46, float 1.000000e+00, float %251, float %256), !dx.precise !30  ; FMad(a,b,c)
  %258 = fmul float %149, %240
  %259 = call float @dx.op.tertiary.f32(i32 46, float %150, float %244, float %258), !dx.precise !30  ; FMad(a,b,c)
  %260 = call float @dx.op.tertiary.f32(i32 46, float %151, float %248, float %259), !dx.precise !30  ; FMad(a,b,c)
  %261 = call float @dx.op.tertiary.f32(i32 46, float 1.000000e+00, float %252, float %260), !dx.precise !30  ; FMad(a,b,c)
  %262 = fmul float %149, %241
  %263 = call float @dx.op.tertiary.f32(i32 46, float %150, float %245, float %262), !dx.precise !30  ; FMad(a,b,c)
  %264 = call float @dx.op.tertiary.f32(i32 46, float %151, float %249, float %263), !dx.precise !30  ; FMad(a,b,c)
  %265 = call float @dx.op.tertiary.f32(i32 46, float 1.000000e+00, float %253, float %264), !dx.precise !30  ; FMad(a,b,c)
  %266 = fdiv float %257, %265
  %267 = fdiv float %261, %265
  %268 = call float @dx.op.unary.f32(i32 6, float %266), !dx.precise !30  ; FAbs(value)
  %269 = call float @dx.op.unary.f32(i32 6, float %267), !dx.precise !30  ; FAbs(value)
  %270 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %9, i32 367)  ; CBufferLoadLegacy(handle,regIndex)
  %271 = extractvalue %dx.types.CBufRet.f32 %270, 0
  %272 = extractvalue %dx.types.CBufRet.f32 %270, 1
  %273 = fcmp ole float %268, %271
  %274 = fcmp ole float %269, %272
  %275 = and i1 %273, %274
  %276 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %9, i32 371)  ; CBufferLoadLegacy(handle,regIndex)
  %277 = extractvalue %dx.types.CBufRet.f32 %276, 0
  %278 = fcmp ogt float %277, 0xC7EFFFFFE0000000
  %279 = and i1 %275, %278
  br i1 %279, label %324, label %280

; <label>:280                                     ; preds = %237
  %281 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %9, i32 361)  ; CBufferLoadLegacy(handle,regIndex)
  %282 = extractvalue %dx.types.CBufRet.f32 %281, 0
  %283 = extractvalue %dx.types.CBufRet.f32 %281, 1
  %284 = extractvalue %dx.types.CBufRet.f32 %281, 3
  %285 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %9, i32 362)  ; CBufferLoadLegacy(handle,regIndex)
  %286 = extractvalue %dx.types.CBufRet.f32 %285, 0
  %287 = extractvalue %dx.types.CBufRet.f32 %285, 1
  %288 = extractvalue %dx.types.CBufRet.f32 %285, 3
  %289 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %9, i32 363)  ; CBufferLoadLegacy(handle,regIndex)
  %290 = extractvalue %dx.types.CBufRet.f32 %289, 0
  %291 = extractvalue %dx.types.CBufRet.f32 %289, 1
  %292 = extractvalue %dx.types.CBufRet.f32 %289, 3
  %293 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %9, i32 364)  ; CBufferLoadLegacy(handle,regIndex)
  %294 = extractvalue %dx.types.CBufRet.f32 %293, 0
  %295 = extractvalue %dx.types.CBufRet.f32 %293, 1
  %296 = extractvalue %dx.types.CBufRet.f32 %293, 3
  %297 = fmul float %149, %282
  %298 = call float @dx.op.tertiary.f32(i32 46, float %150, float %286, float %297), !dx.precise !30  ; FMad(a,b,c)
  %299 = call float @dx.op.tertiary.f32(i32 46, float %151, float %290, float %298), !dx.precise !30  ; FMad(a,b,c)
  %300 = call float @dx.op.tertiary.f32(i32 46, float 1.000000e+00, float %294, float %299), !dx.precise !30  ; FMad(a,b,c)
  %301 = fmul float %149, %283
  %302 = call float @dx.op.tertiary.f32(i32 46, float %150, float %287, float %301), !dx.precise !30  ; FMad(a,b,c)
  %303 = call float @dx.op.tertiary.f32(i32 46, float %151, float %291, float %302), !dx.precise !30  ; FMad(a,b,c)
  %304 = call float @dx.op.tertiary.f32(i32 46, float 1.000000e+00, float %295, float %303), !dx.precise !30  ; FMad(a,b,c)
  %305 = fmul float %149, %284
  %306 = call float @dx.op.tertiary.f32(i32 46, float %150, float %288, float %305), !dx.precise !30  ; FMad(a,b,c)
  %307 = call float @dx.op.tertiary.f32(i32 46, float %151, float %292, float %306), !dx.precise !30  ; FMad(a,b,c)
  %308 = call float @dx.op.tertiary.f32(i32 46, float 1.000000e+00, float %296, float %307), !dx.precise !30  ; FMad(a,b,c)
  %309 = fdiv float %300, %308
  %310 = fdiv float %304, %308
  %311 = call float @dx.op.unary.f32(i32 6, float %309), !dx.precise !30  ; FAbs(value)
  %312 = call float @dx.op.unary.f32(i32 6, float %310), !dx.precise !30  ; FAbs(value)
  %313 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %9, i32 368)  ; CBufferLoadLegacy(handle,regIndex)
  %314 = extractvalue %dx.types.CBufRet.f32 %313, 0
  %315 = extractvalue %dx.types.CBufRet.f32 %313, 1
  %316 = fcmp ole float %311, %314
  %317 = fcmp ole float %312, %315
  %318 = and i1 %316, %317
  %319 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %9, i32 372)  ; CBufferLoadLegacy(handle,regIndex)
  %320 = extractvalue %dx.types.CBufRet.f32 %319, 0
  %321 = fcmp ogt float %320, 0xC7EFFFFFE0000000
  %322 = and i1 %318, %321
  %323 = select i1 %322, i32 3, i32 -1
  br label %324

; <label>:324                                     ; preds = %280, %237, %194, %0
  %325 = phi i32 [ 0, %0 ], [ 1, %194 ], [ 2, %237 ], [ %323, %280 ]
  %326 = icmp eq i32 %325, -1
  %327 = select i1 %326, i32 0, i32 %325
  %328 = fsub float -0.000000e+00, %40
  %329 = fsub float -0.000000e+00, %41
  %330 = fsub float -0.000000e+00, %36
  %331 = fsub float -0.000000e+00, %37
  %332 = fsub float %81, %36
  %333 = fsub float %82, %37
  %334 = fsub float %332, %81
  %335 = fsub float %333, %82
  %336 = fsub float %332, %334
  %337 = fsub float %333, %335
  %338 = fsub float %81, %336
  %339 = fsub float %82, %337
  %340 = fsub float %330, %334
  %341 = fsub float %331, %335
  %342 = fadd float %340, %338
  %343 = fadd float %341, %339
  %344 = fsub float 0.000000e+00, %40
  %345 = fsub float 0.000000e+00, %41
  %346 = fsub float %344, %344
  %347 = fsub float %345, %345
  %348 = fsub float 0.000000e+00, %346
  %349 = fsub float 0.000000e+00, %347
  %350 = fsub float %328, %344
  %351 = fsub float %329, %345
  %352 = fadd float %350, %348
  %353 = fadd float %351, %349
  %354 = fadd float %344, %342
  %355 = fadd float %345, %343
  %356 = fadd float %332, %354
  %357 = fadd float %333, %355
  %358 = fsub float %356, %332
  %359 = fsub float %357, %333
  %360 = fsub float %354, %358
  %361 = fsub float %355, %359
  %362 = fadd float %352, %360
  %363 = fadd float %353, %361
  %364 = fadd float %356, %362
  %365 = fadd float %357, %363
  %366 = fsub float %364, %356
  %367 = fsub float %365, %357
  %368 = fsub float %362, %366
  %369 = fsub float %363, %367
  %370 = fmul float %364, 0x3EA0000000000000
  %371 = fmul float %365, 0x3EA0000000000000
  %372 = fadd float %370, 5.000000e-01
  %373 = fadd float %371, 5.000000e-01
  %374 = call float @dx.op.unary.f32(i32 27, float %372), !dx.precise !30  ; Round_ni(value)
  %375 = call float @dx.op.unary.f32(i32 27, float %373), !dx.precise !30  ; Round_ni(value)
  %376 = call float @dx.op.tertiary.f32(i32 46, float %374, float -2.097152e+06, float %364), !dx.precise !30  ; FMad(a,b,c)
  %377 = call float @dx.op.tertiary.f32(i32 46, float %375, float -2.097152e+06, float %365), !dx.precise !30  ; FMad(a,b,c)
  %378 = fadd float %376, %368
  %379 = fadd float %377, %369
  %380 = shl nsw i32 %327, 1
  %381 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %3, %dx.types.ResourceProperties { i32 10, i32 1033 })  ; AnnotateHandle(res,props)  resource: TypedBuffer<4xF32>
  %382 = call %dx.types.ResRet.f32 @dx.op.bufferLoad.f32(i32 68, %dx.types.Handle %381, i32 %380, i32 undef)  ; BufferLoad(srv,index,wot)
  %383 = extractvalue %dx.types.ResRet.f32 %382, 1
  %384 = extractvalue %dx.types.ResRet.f32 %382, 2
  %385 = fptosi float %383 to i32
  %386 = fptosi float %384 to i32
  %387 = call i32 @dx.op.binary.i32(i32 38, i32 %386, i32 %91)  ; IMin(a,b)
  %388 = icmp sgt i32 %387, 0
  br i1 %388, label %389, label %882

; <label>:389                                     ; preds = %324
  br label %390

; <label>:390                                     ; preds = %809, %389
  %391 = phi float [ %878, %809 ], [ 0.000000e+00, %389 ]
  %392 = phi i32 [ %879, %809 ], [ 0, %389 ]
  %393 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %2, %dx.types.ResourceProperties { i32 10, i32 1033 })  ; AnnotateHandle(res,props)  resource: TypedBuffer<4xF32>
  %394 = call %dx.types.ResRet.f32 @dx.op.bufferLoad.f32(i32 68, %dx.types.Handle %393, i32 0, i32 undef)  ; BufferLoad(srv,index,wot)
  %395 = extractvalue %dx.types.ResRet.f32 %394, 1
  %396 = fptosi float %395 to i32
  %397 = add nsw i32 %392, %385
  %398 = shl nsw i32 %397, 2
  %399 = add nsw i32 %396, %398
  %400 = add nsw i32 %399, 2
  %401 = call %dx.types.ResRet.f32 @dx.op.bufferLoad.f32(i32 68, %dx.types.Handle %393, i32 %400, i32 undef)  ; BufferLoad(srv,index,wot)
  %402 = extractvalue %dx.types.ResRet.f32 %401, 0
  %403 = extractvalue %dx.types.ResRet.f32 %401, 1
  %404 = extractvalue %dx.types.ResRet.f32 %401, 2
  %405 = extractvalue %dx.types.ResRet.f32 %401, 3
  %406 = add nsw i32 %399, 3
  %407 = call %dx.types.ResRet.f32 @dx.op.bufferLoad.f32(i32 68, %dx.types.Handle %393, i32 %406, i32 undef)  ; BufferLoad(srv,index,wot)
  %408 = extractvalue %dx.types.ResRet.f32 %407, 0
  %409 = extractvalue %dx.types.ResRet.f32 %407, 1
  %410 = fmul float %90, %405
  %411 = fmul float %378, 0x3EA0000000000000
  %412 = fmul float %379, 0x3EA0000000000000
  %413 = fadd float %411, 5.000000e-01
  %414 = fadd float %412, 5.000000e-01
  %415 = call float @dx.op.unary.f32(i32 27, float %413), !dx.precise !30  ; Round_ni(value)
  %416 = call float @dx.op.unary.f32(i32 27, float %414), !dx.precise !30  ; Round_ni(value)
  %417 = call float @dx.op.tertiary.f32(i32 46, float %415, float -2.097152e+06, float %378), !dx.precise !30  ; FMad(a,b,c)
  %418 = call float @dx.op.tertiary.f32(i32 46, float %416, float -2.097152e+06, float %379), !dx.precise !30  ; FMad(a,b,c)
  %419 = fadd float %417, 0.000000e+00
  %420 = fadd float %418, 0.000000e+00
  %421 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %9, i32 72)  ; CBufferLoadLegacy(handle,regIndex)
  %422 = extractvalue %dx.types.CBufRet.f32 %421, 0
  %423 = extractvalue %dx.types.CBufRet.f32 %421, 1
  %424 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %9, i32 80)  ; CBufferLoadLegacy(handle,regIndex)
  %425 = extractvalue %dx.types.CBufRet.f32 %424, 0
  %426 = extractvalue %dx.types.CBufRet.f32 %424, 1
  %427 = fsub float -0.000000e+00, %425
  %428 = fsub float -0.000000e+00, %426
  %429 = fsub float -0.000000e+00, %422
  %430 = fsub float -0.000000e+00, %423
  %431 = fsub float %378, %422
  %432 = fsub float %379, %423
  %433 = fsub float %431, %378
  %434 = fsub float %432, %379
  %435 = fsub float %431, %433
  %436 = fsub float %432, %434
  %437 = fsub float %378, %435
  %438 = fsub float %379, %436
  %439 = fsub float %429, %433
  %440 = fsub float %430, %434
  %441 = fadd float %439, %437
  %442 = fadd float %440, %438
  %443 = fsub float 0.000000e+00, %425
  %444 = fsub float 0.000000e+00, %426
  %445 = fsub float %443, %443
  %446 = fsub float %444, %444
  %447 = fsub float 0.000000e+00, %445
  %448 = fsub float 0.000000e+00, %446
  %449 = fsub float %427, %443
  %450 = fsub float %428, %444
  %451 = fadd float %449, %447
  %452 = fadd float %450, %448
  %453 = fadd float %443, %441
  %454 = fadd float %444, %442
  %455 = fadd float %431, %453
  %456 = fadd float %432, %454
  %457 = fsub float %455, %431
  %458 = fsub float %456, %432
  %459 = fsub float %453, %457
  %460 = fsub float %454, %458
  %461 = fadd float %451, %459
  %462 = fadd float %452, %460
  %463 = fadd float %455, %461
  %464 = fadd float %456, %462
  %465 = fsub float %463, %455
  %466 = fsub float %464, %456
  %467 = fsub float %461, %465
  %468 = fsub float %462, %466
  %469 = fmul float %463, 0x3EA0000000000000
  %470 = fmul float %464, 0x3EA0000000000000
  %471 = fadd float %469, 5.000000e-01
  %472 = fadd float %470, 5.000000e-01
  %473 = call float @dx.op.unary.f32(i32 27, float %471), !dx.precise !30  ; Round_ni(value)
  %474 = call float @dx.op.unary.f32(i32 27, float %472), !dx.precise !30  ; Round_ni(value)
  %475 = call float @dx.op.tertiary.f32(i32 46, float %473, float -2.097152e+06, float %463), !dx.precise !30  ; FMad(a,b,c)
  %476 = call float @dx.op.tertiary.f32(i32 46, float %474, float -2.097152e+06, float %464), !dx.precise !30  ; FMad(a,b,c)
  %477 = fadd float %475, %467
  %478 = fadd float %476, %468
  %479 = fmul float %477, %477
  %480 = fmul float %478, %478
  %481 = fadd float %479, %480
  %482 = call float @dx.op.unary.f32(i32 24, float %481), !dx.precise !30  ; Sqrt(value)
  %483 = call float @dx.op.dot2.f32(i32 54, float %419, float %420, float %408, float %409), !dx.precise !30  ; Dot2(ax,ay,bx,by)
  %484 = fsub float %483, %410
  %485 = call float @dx.op.unary.f32(i32 13, float %484), !dx.precise !30  ; Sin(value)
  %486 = call float @dx.op.unary.f32(i32 6, float %419), !dx.precise !30  ; FAbs(value)
  %487 = call float @dx.op.unary.f32(i32 6, float %420), !dx.precise !30  ; FAbs(value)
  %488 = fsub float 1.048576e+06, %486
  %489 = fsub float 1.048576e+06, %487
  %490 = fcmp olt float %488, 4.000000e+02
  %491 = fcmp olt float %489, 4.000000e+02
  %492 = or i1 %490, %491
  br i1 %492, label %493, label %505

; <label>:493                                     ; preds = %390
  %494 = fdiv float %488, 4.000000e+02
  %495 = fdiv float %489, 4.000000e+02
  %496 = call float @dx.op.unary.f32(i32 7, float %494), !dx.precise !30  ; Saturate(value)
  %497 = call float @dx.op.unary.f32(i32 7, float %495), !dx.precise !30  ; Saturate(value)
  %498 = fmul float %496, %497
  %499 = call float @dx.op.dot2.f32(i32 54, float %488, float %489, float %408, float %409), !dx.precise !30  ; Dot2(ax,ay,bx,by)
  %500 = fsub float %499, %410
  %501 = call float @dx.op.unary.f32(i32 13, float %500), !dx.precise !30  ; Sin(value)
  %502 = fsub float %485, %501
  %503 = fmul float %498, %502
  %504 = fadd float %501, %503
  br label %505

; <label>:505                                     ; preds = %493, %390
  %506 = phi float [ %504, %493 ], [ %485, %390 ]
  %507 = fmul float %402, %506
  %508 = fmul float %403, %506
  %509 = fmul float %482, 0x3F60624DE0000000
  %510 = call float @dx.op.unary.f32(i32 7, float %509), !dx.precise !30  ; Saturate(value)
  %511 = fmul float %507, %510
  %512 = fmul float %508, %510
  %513 = fsub float %378, %511
  %514 = fsub float %379, %512
  %515 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %9, i32 555)  ; CBufferLoadLegacy(handle,regIndex)
  %516 = extractvalue %dx.types.CBufRet.f32 %515, 3
  %517 = fmul float %516, 1.562500e-02
  %518 = fdiv float %513, %516
  %519 = fdiv float %514, %516
  %520 = call float @dx.op.unary.f32(i32 27, float %518), !dx.precise !30  ; Round_ni(value)
  %521 = call float @dx.op.unary.f32(i32 27, float %519), !dx.precise !30  ; Round_ni(value)
  %522 = fmul float %516, %520
  %523 = fmul float %516, %521
  %524 = fdiv float %513, %517
  %525 = fdiv float %514, %517
  %526 = call float @dx.op.unary.f32(i32 27, float %524), !dx.precise !30  ; Round_ni(value)
  %527 = call float @dx.op.unary.f32(i32 27, float %525), !dx.precise !30  ; Round_ni(value)
  %528 = fmul float %517, %526
  %529 = fmul float %517, %527
  %530 = fadd float %517, %528
  %531 = fadd float %517, %529
  %532 = fsub float %528, %522
  %533 = fsub float %529, %523
  %534 = fdiv float %532, %517
  %535 = fdiv float %533, %517
  %536 = call float @dx.op.unary.f32(i32 27, float %534), !dx.precise !30  ; Round_ni(value)
  %537 = call float @dx.op.unary.f32(i32 27, float %535), !dx.precise !30  ; Round_ni(value)
  %538 = fptoui float %536 to i32
  %539 = fptoui float %537 to i32
  %540 = xor i32 %538, %539
  %541 = and i32 %540, 1
  %542 = icmp eq i32 %541, 0
  %543 = select i1 %542, float %528, float %530
  %544 = select i1 %542, float %530, float %528
  %545 = fsub float %513, %544
  %546 = fsub float %514, %529
  %547 = call float @dx.op.dot2.f32(i32 54, float %545, float %546, float %545, float %546), !dx.precise !30  ; Dot2(ax,ay,bx,by)
  %548 = fsub float %513, %543
  %549 = fsub float %514, %531
  %550 = call float @dx.op.dot2.f32(i32 54, float %548, float %549, float %548, float %549), !dx.precise !30  ; Dot2(ax,ay,bx,by)
  %551 = fcmp olt float %547, %550
  %552 = select i1 %551, float %544, float %543
  %553 = select i1 %551, float %529, float %531
  %554 = fmul float %543, 0x3EA0000000000000
  %555 = fmul float %529, 0x3EA0000000000000
  %556 = fadd float %554, 5.000000e-01
  %557 = fadd float %555, 5.000000e-01
  %558 = call float @dx.op.unary.f32(i32 27, float %556), !dx.precise !30  ; Round_ni(value)
  %559 = call float @dx.op.unary.f32(i32 27, float %557), !dx.precise !30  ; Round_ni(value)
  %560 = call float @dx.op.tertiary.f32(i32 46, float %558, float -2.097152e+06, float %543), !dx.precise !30  ; FMad(a,b,c)
  %561 = call float @dx.op.tertiary.f32(i32 46, float %559, float -2.097152e+06, float %529), !dx.precise !30  ; FMad(a,b,c)
  %562 = fadd float %560, 0.000000e+00
  %563 = fadd float %561, 0.000000e+00
  %564 = fsub float %543, %422
  %565 = fsub float %529, %423
  %566 = fsub float %564, %543
  %567 = fsub float %565, %529
  %568 = fsub float %564, %566
  %569 = fsub float %565, %567
  %570 = fsub float %543, %568
  %571 = fsub float %529, %569
  %572 = fsub float %429, %566
  %573 = fsub float %430, %567
  %574 = fadd float %572, %570
  %575 = fadd float %573, %571
  %576 = fadd float %443, %574
  %577 = fadd float %444, %575
  %578 = fadd float %564, %576
  %579 = fadd float %565, %577
  %580 = fsub float %578, %564
  %581 = fsub float %579, %565
  %582 = fsub float %576, %580
  %583 = fsub float %577, %581
  %584 = fadd float %451, %582
  %585 = fadd float %452, %583
  %586 = fadd float %578, %584
  %587 = fadd float %579, %585
  %588 = fsub float %586, %578
  %589 = fsub float %587, %579
  %590 = fsub float %584, %588
  %591 = fsub float %585, %589
  %592 = fmul float %586, 0x3EA0000000000000
  %593 = fmul float %587, 0x3EA0000000000000
  %594 = fadd float %592, 5.000000e-01
  %595 = fadd float %593, 5.000000e-01
  %596 = call float @dx.op.unary.f32(i32 27, float %594), !dx.precise !30  ; Round_ni(value)
  %597 = call float @dx.op.unary.f32(i32 27, float %595), !dx.precise !30  ; Round_ni(value)
  %598 = call float @dx.op.tertiary.f32(i32 46, float %596, float -2.097152e+06, float %586), !dx.precise !30  ; FMad(a,b,c)
  %599 = call float @dx.op.tertiary.f32(i32 46, float %597, float -2.097152e+06, float %587), !dx.precise !30  ; FMad(a,b,c)
  %600 = fadd float %598, %590
  %601 = fadd float %599, %591
  %602 = fmul fast float %600, %600
  %603 = fmul fast float %601, %601
  %604 = fadd fast float %602, %603
  %605 = call float @dx.op.unary.f32(i32 24, float %604)  ; Sqrt(value)
  %606 = call float @dx.op.dot2.f32(i32 54, float %562, float %563, float %408, float %409)  ; Dot2(ax,ay,bx,by)
  %607 = fsub fast float %606, %410
  %608 = call float @dx.op.unary.f32(i32 13, float %607)  ; Sin(value)
  %609 = call float @dx.op.unary.f32(i32 12, float %607)  ; Cos(value)
  %610 = call float @dx.op.unary.f32(i32 6, float %562)  ; FAbs(value)
  %611 = call float @dx.op.unary.f32(i32 6, float %563)  ; FAbs(value)
  %612 = fsub fast float 1.048576e+06, %610
  %613 = fsub fast float 1.048576e+06, %611
  %614 = fcmp fast olt float %612, 4.000000e+02
  %615 = fcmp fast olt float %613, 4.000000e+02
  %616 = or i1 %614, %615
  br i1 %616, label %617, label %633

; <label>:617                                     ; preds = %505
  %618 = fmul fast float %612, 0x3F647AE140000000
  %619 = fmul fast float %613, 0x3F647AE140000000
  %620 = call float @dx.op.unary.f32(i32 7, float %618)  ; Saturate(value)
  %621 = call float @dx.op.unary.f32(i32 7, float %619)  ; Saturate(value)
  %622 = fmul fast float %621, %620
  %623 = call float @dx.op.dot2.f32(i32 54, float %612, float %613, float %408, float %409)  ; Dot2(ax,ay,bx,by)
  %624 = fsub fast float %623, %410
  %625 = call float @dx.op.unary.f32(i32 13, float %624)  ; Sin(value)
  %626 = fsub fast float %608, %625
  %627 = fmul fast float %626, %622
  %628 = fadd fast float %627, %625
  %629 = call float @dx.op.unary.f32(i32 12, float %624)  ; Cos(value)
  %630 = fsub fast float %609, %629
  %631 = fmul fast float %630, %622
  %632 = fadd fast float %631, %629
  br label %633

; <label>:633                                     ; preds = %617, %505
  %634 = phi float [ %628, %617 ], [ %608, %505 ]
  %635 = phi float [ %632, %617 ], [ %609, %505 ]
  %636 = fmul fast float %634, %402
  %637 = fmul fast float %634, %403
  %638 = fmul fast float %605, 0x3F60624DE0000000
  %639 = call float @dx.op.unary.f32(i32 7, float %638)  ; Saturate(value)
  %640 = fmul fast float %636, %639
  %641 = fmul fast float %637, %639
  %642 = fmul float %544, 0x3EA0000000000000
  %643 = fmul float %531, 0x3EA0000000000000
  %644 = fadd float %642, 5.000000e-01
  %645 = fadd float %643, 5.000000e-01
  %646 = call float @dx.op.unary.f32(i32 27, float %644), !dx.precise !30  ; Round_ni(value)
  %647 = call float @dx.op.unary.f32(i32 27, float %645), !dx.precise !30  ; Round_ni(value)
  %648 = call float @dx.op.tertiary.f32(i32 46, float %646, float -2.097152e+06, float %544), !dx.precise !30  ; FMad(a,b,c)
  %649 = call float @dx.op.tertiary.f32(i32 46, float %647, float -2.097152e+06, float %531), !dx.precise !30  ; FMad(a,b,c)
  %650 = fadd float %648, 0.000000e+00
  %651 = fadd float %649, 0.000000e+00
  %652 = fsub float %544, %422
  %653 = fsub float %531, %423
  %654 = fsub float %652, %544
  %655 = fsub float %653, %531
  %656 = fsub float %652, %654
  %657 = fsub float %653, %655
  %658 = fsub float %544, %656
  %659 = fsub float %531, %657
  %660 = fsub float %429, %654
  %661 = fsub float %430, %655
  %662 = fadd float %660, %658
  %663 = fadd float %661, %659
  %664 = fadd float %443, %662
  %665 = fadd float %444, %663
  %666 = fadd float %652, %664
  %667 = fadd float %653, %665
  %668 = fsub float %666, %652
  %669 = fsub float %667, %653
  %670 = fsub float %664, %668
  %671 = fsub float %665, %669
  %672 = fadd float %451, %670
  %673 = fadd float %452, %671
  %674 = fadd float %666, %672
  %675 = fadd float %667, %673
  %676 = fsub float %674, %666
  %677 = fsub float %675, %667
  %678 = fsub float %672, %676
  %679 = fsub float %673, %677
  %680 = fmul float %674, 0x3EA0000000000000
  %681 = fmul float %675, 0x3EA0000000000000
  %682 = fadd float %680, 5.000000e-01
  %683 = fadd float %681, 5.000000e-01
  %684 = call float @dx.op.unary.f32(i32 27, float %682), !dx.precise !30  ; Round_ni(value)
  %685 = call float @dx.op.unary.f32(i32 27, float %683), !dx.precise !30  ; Round_ni(value)
  %686 = call float @dx.op.tertiary.f32(i32 46, float %684, float -2.097152e+06, float %674), !dx.precise !30  ; FMad(a,b,c)
  %687 = call float @dx.op.tertiary.f32(i32 46, float %685, float -2.097152e+06, float %675), !dx.precise !30  ; FMad(a,b,c)
  %688 = fadd float %686, %678
  %689 = fadd float %687, %679
  %690 = fmul fast float %688, %688
  %691 = fmul fast float %689, %689
  %692 = fadd fast float %690, %691
  %693 = call float @dx.op.unary.f32(i32 24, float %692)  ; Sqrt(value)
  %694 = call float @dx.op.dot2.f32(i32 54, float %650, float %651, float %408, float %409)  ; Dot2(ax,ay,bx,by)
  %695 = fsub fast float %694, %410
  %696 = call float @dx.op.unary.f32(i32 13, float %695)  ; Sin(value)
  %697 = call float @dx.op.unary.f32(i32 12, float %695)  ; Cos(value)
  %698 = call float @dx.op.unary.f32(i32 6, float %650)  ; FAbs(value)
  %699 = call float @dx.op.unary.f32(i32 6, float %651)  ; FAbs(value)
  %700 = fsub fast float 1.048576e+06, %698
  %701 = fsub fast float 1.048576e+06, %699
  %702 = fcmp fast olt float %700, 4.000000e+02
  %703 = fcmp fast olt float %701, 4.000000e+02
  %704 = or i1 %702, %703
  br i1 %704, label %705, label %721

; <label>:705                                     ; preds = %633
  %706 = fmul fast float %700, 0x3F647AE140000000
  %707 = fmul fast float %701, 0x3F647AE140000000
  %708 = call float @dx.op.unary.f32(i32 7, float %706)  ; Saturate(value)
  %709 = call float @dx.op.unary.f32(i32 7, float %707)  ; Saturate(value)
  %710 = fmul fast float %709, %708
  %711 = call float @dx.op.dot2.f32(i32 54, float %700, float %701, float %408, float %409)  ; Dot2(ax,ay,bx,by)
  %712 = fsub fast float %711, %410
  %713 = call float @dx.op.unary.f32(i32 13, float %712)  ; Sin(value)
  %714 = fsub fast float %696, %713
  %715 = fmul fast float %714, %710
  %716 = fadd fast float %715, %713
  %717 = call float @dx.op.unary.f32(i32 12, float %712)  ; Cos(value)
  %718 = fsub fast float %697, %717
  %719 = fmul fast float %718, %710
  %720 = fadd fast float %719, %717
  br label %721

; <label>:721                                     ; preds = %705, %633
  %722 = phi float [ %716, %705 ], [ %696, %633 ]
  %723 = phi float [ %720, %705 ], [ %697, %633 ]
  %724 = fmul fast float %722, %402
  %725 = fmul fast float %722, %403
  %726 = fmul fast float %693, 0x3F60624DE0000000
  %727 = call float @dx.op.unary.f32(i32 7, float %726)  ; Saturate(value)
  %728 = fmul fast float %724, %727
  %729 = fmul fast float %725, %727
  %730 = fmul float %552, 0x3EA0000000000000
  %731 = fmul float %553, 0x3EA0000000000000
  %732 = fadd float %730, 5.000000e-01
  %733 = fadd float %731, 5.000000e-01
  %734 = call float @dx.op.unary.f32(i32 27, float %732), !dx.precise !30  ; Round_ni(value)
  %735 = call float @dx.op.unary.f32(i32 27, float %733), !dx.precise !30  ; Round_ni(value)
  %736 = call float @dx.op.tertiary.f32(i32 46, float %734, float -2.097152e+06, float %552), !dx.precise !30  ; FMad(a,b,c)
  %737 = call float @dx.op.tertiary.f32(i32 46, float %735, float -2.097152e+06, float %553), !dx.precise !30  ; FMad(a,b,c)
  %738 = fadd float %736, 0.000000e+00
  %739 = fadd float %737, 0.000000e+00
  %740 = fsub float %552, %422
  %741 = fsub float %553, %423
  %742 = fsub float %740, %552
  %743 = fsub float %741, %553
  %744 = fsub float %740, %742
  %745 = fsub float %741, %743
  %746 = fsub float %552, %744
  %747 = fsub float %553, %745
  %748 = fsub float %429, %742
  %749 = fsub float %430, %743
  %750 = fadd float %748, %746
  %751 = fadd float %749, %747
  %752 = fadd float %443, %750
  %753 = fadd float %444, %751
  %754 = fadd float %740, %752
  %755 = fadd float %741, %753
  %756 = fsub float %754, %740
  %757 = fsub float %755, %741
  %758 = fsub float %752, %756
  %759 = fsub float %753, %757
  %760 = fadd float %451, %758
  %761 = fadd float %452, %759
  %762 = fadd float %754, %760
  %763 = fadd float %755, %761
  %764 = fsub float %762, %754
  %765 = fsub float %763, %755
  %766 = fsub float %760, %764
  %767 = fsub float %761, %765
  %768 = fmul float %762, 0x3EA0000000000000
  %769 = fmul float %763, 0x3EA0000000000000
  %770 = fadd float %768, 5.000000e-01
  %771 = fadd float %769, 5.000000e-01
  %772 = call float @dx.op.unary.f32(i32 27, float %770), !dx.precise !30  ; Round_ni(value)
  %773 = call float @dx.op.unary.f32(i32 27, float %771), !dx.precise !30  ; Round_ni(value)
  %774 = call float @dx.op.tertiary.f32(i32 46, float %772, float -2.097152e+06, float %762), !dx.precise !30  ; FMad(a,b,c)
  %775 = call float @dx.op.tertiary.f32(i32 46, float %773, float -2.097152e+06, float %763), !dx.precise !30  ; FMad(a,b,c)
  %776 = fadd float %774, %766
  %777 = fadd float %775, %767
  %778 = fmul fast float %776, %776
  %779 = fmul fast float %777, %777
  %780 = fadd fast float %778, %779
  %781 = call float @dx.op.unary.f32(i32 24, float %780)  ; Sqrt(value)
  %782 = call float @dx.op.dot2.f32(i32 54, float %738, float %739, float %408, float %409)  ; Dot2(ax,ay,bx,by)
  %783 = fsub fast float %782, %410
  %784 = call float @dx.op.unary.f32(i32 13, float %783)  ; Sin(value)
  %785 = call float @dx.op.unary.f32(i32 12, float %783)  ; Cos(value)
  %786 = call float @dx.op.unary.f32(i32 6, float %738)  ; FAbs(value)
  %787 = call float @dx.op.unary.f32(i32 6, float %739)  ; FAbs(value)
  %788 = fsub fast float 1.048576e+06, %786
  %789 = fsub fast float 1.048576e+06, %787
  %790 = fcmp fast olt float %788, 4.000000e+02
  %791 = fcmp fast olt float %789, 4.000000e+02
  %792 = or i1 %790, %791
  br i1 %792, label %793, label %809

; <label>:793                                     ; preds = %721
  %794 = fmul fast float %788, 0x3F647AE140000000
  %795 = fmul fast float %789, 0x3F647AE140000000
  %796 = call float @dx.op.unary.f32(i32 7, float %794)  ; Saturate(value)
  %797 = call float @dx.op.unary.f32(i32 7, float %795)  ; Saturate(value)
  %798 = fmul fast float %797, %796
  %799 = call float @dx.op.dot2.f32(i32 54, float %788, float %789, float %408, float %409)  ; Dot2(ax,ay,bx,by)
  %800 = fsub fast float %799, %410
  %801 = call float @dx.op.unary.f32(i32 13, float %800)  ; Sin(value)
  %802 = fsub fast float %784, %801
  %803 = fmul fast float %802, %798
  %804 = fadd fast float %803, %801
  %805 = call float @dx.op.unary.f32(i32 12, float %800)  ; Cos(value)
  %806 = fsub fast float %785, %805
  %807 = fmul fast float %806, %798
  %808 = fadd fast float %807, %805
  br label %809

; <label>:809                                     ; preds = %793, %721
  %810 = phi float [ %804, %793 ], [ %784, %721 ]
  %811 = phi float [ %808, %793 ], [ %785, %721 ]
  %812 = fmul fast float %810, %402
  %813 = fmul fast float %810, %403
  %814 = fmul fast float %781, 0x3F60624DE0000000
  %815 = call float @dx.op.unary.f32(i32 7, float %814)  ; Saturate(value)
  %816 = fmul fast float %812, %815
  %817 = fmul fast float %813, %815
  %818 = fadd fast float %640, %543
  %819 = fadd fast float %641, %529
  %820 = fadd fast float %728, %544
  %821 = fadd fast float %729, %531
  %822 = fadd fast float %816, %552
  %823 = fadd fast float %817, %553
  %824 = fsub fast float %379, %823
  %825 = fsub fast float %822, %378
  %826 = fmul fast float %824, %822
  %827 = fmul fast float %825, %823
  %828 = fadd fast float %826, %827
  %829 = fsub fast float %821, %819
  %830 = fsub fast float %818, %820
  %831 = fmul fast float %829, %818
  %832 = fmul fast float %830, %819
  %833 = fadd fast float %831, %832
  %834 = fmul fast float %824, %830
  %835 = fmul fast float %825, %829
  %836 = fsub fast float %834, %835
  %837 = fmul fast float %828, %830
  %838 = fmul fast float %825, %833
  %839 = fsub fast float %837, %838
  %840 = fdiv fast float %839, %836
  %841 = fmul fast float %824, %833
  %842 = fmul fast float %828, %829
  %843 = fsub fast float %841, %842
  %844 = fdiv fast float %843, %836
  %845 = fsub fast float %840, %818
  %846 = fsub fast float %844, %819
  %847 = fmul fast float %845, %845
  %848 = fmul fast float %846, %846
  %849 = fadd fast float %848, %847
  %850 = call float @dx.op.unary.f32(i32 24, float %849)  ; Sqrt(value)
  %851 = fsub fast float %820, %818
  %852 = fmul fast float %851, %851
  %853 = fmul fast float %829, %829
  %854 = fadd fast float %852, %853
  %855 = call float @dx.op.unary.f32(i32 24, float %854)  ; Sqrt(value)
  %856 = fdiv fast float %850, %855
  %857 = fsub fast float %378, %840
  %858 = fsub fast float %379, %844
  %859 = fmul fast float %857, %857
  %860 = fmul fast float %858, %858
  %861 = fadd fast float %860, %859
  %862 = call float @dx.op.unary.f32(i32 24, float %861)  ; Sqrt(value)
  %863 = fsub fast float %822, %840
  %864 = fsub fast float %823, %844
  %865 = fmul fast float %863, %863
  %866 = fmul fast float %864, %864
  %867 = fadd fast float %865, %866
  %868 = call float @dx.op.unary.f32(i32 24, float %867)  ; Sqrt(value)
  %869 = fdiv fast float %862, %868
  %870 = fsub fast float %723, %635
  %871 = fmul fast float %856, %870
  %872 = fadd fast float %871, %635
  %873 = fmul fast float %872, %404
  %874 = fsub fast float %811, %872
  %875 = fmul fast float %404, %874
  %876 = fmul fast float %875, %869
  %877 = fadd fast float %873, %391
  %878 = fadd fast float %877, %876
  %879 = add nuw nsw i32 %392, 1
  %880 = icmp eq i32 %879, %387
  br i1 %880, label %881, label %390

; <label>:881                                     ; preds = %809
  br label %882

; <label>:882                                     ; preds = %881, %324
  %883 = phi float [ 0.000000e+00, %324 ], [ %878, %881 ]
  %884 = add nsw i32 %327, 369
  %885 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %9, i32 %884)  ; CBufferLoadLegacy(handle,regIndex)
  %886 = extractvalue %dx.types.CBufRet.f32 %885, 0
  %887 = extractvalue %dx.types.CBufRet.f32 %87, 1
  %888 = fsub fast float %883, %86
  %889 = fadd fast float %888, %886
  %890 = fsub fast float %889, %887
  %891 = extractvalue %dx.types.CBufRet.f32 %87, 2
  %892 = fmul fast float %890, %891
  %893 = fmul fast float %46, %891
  %894 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %7, i32 1)  ; CBufferLoadLegacy(handle,regIndex)
  %895 = extractvalue %dx.types.CBufRet.f32 %894, 0
  %896 = extractvalue %dx.types.CBufRet.f32 %894, 1
  %897 = extractvalue %dx.types.CBufRet.f32 %894, 2
  %898 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %7, i32 2)  ; CBufferLoadLegacy(handle,regIndex)
  %899 = extractvalue %dx.types.CBufRet.f32 %898, 0
  %900 = extractvalue %dx.types.CBufRet.f32 %898, 1
  %901 = extractvalue %dx.types.CBufRet.f32 %898, 2
  %902 = fmul fast float %893, -2.097152e+06
  %903 = fadd fast float %902, %892
  %904 = fmul fast float %903, %895
  %905 = fadd fast float %904, %899
  %906 = fmul fast float %903, %896
  %907 = fadd fast float %906, %900
  %908 = fmul fast float %903, %897
  %909 = fadd fast float %908, %901
  %910 = call float @dx.op.unary.f32(i32 7, float %905)  ; Saturate(value)
  %911 = call float @dx.op.unary.f32(i32 7, float %907)  ; Saturate(value)
  %912 = call float @dx.op.unary.f32(i32 7, float %909)  ; Saturate(value)
  %913 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %9, i32 347)  ; CBufferLoadLegacy(handle,regIndex)
  %914 = extractvalue %dx.types.CBufRet.f32 %913, 2
  %915 = fcmp fast ogt float %914, 0.000000e+00
  %916 = select i1 %915, float 1.000000e+00, float 0.000000e+00
  %917 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %7, i32 3)  ; CBufferLoadLegacy(handle,regIndex)
  %918 = extractvalue %dx.types.CBufRet.f32 %917, 0
  %919 = extractvalue %dx.types.CBufRet.f32 %917, 1
  %920 = extractvalue %dx.types.CBufRet.f32 %917, 2
  %921 = fmul fast float %916, %918
  %922 = fmul fast float %916, %919
  %923 = fmul fast float %916, %920
  %924 = fsub fast float %48, %84
  %925 = fsub fast float %49, %85
  %926 = fsub fast float %50, %86
  %927 = fmul fast float %924, %924
  %928 = fmul fast float %925, %925
  %929 = fadd fast float %928, %927
  %930 = fmul fast float %926, %926
  %931 = fadd fast float %929, %930
  %932 = call float @dx.op.unary.f32(i32 24, float %931)  ; Sqrt(value)
  %933 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %7, i32 4)  ; CBufferLoadLegacy(handle,regIndex)
  %934 = extractvalue %dx.types.CBufRet.f32 %933, 1
  %935 = extractvalue %dx.types.CBufRet.f32 %933, 0
  %936 = extractvalue %dx.types.CBufRet.f32 %917, 3
  %937 = fcmp fast oge float %932, %936
  %938 = select i1 %937, float %935, float %934
  %939 = fmul fast float %921, %938
  %940 = fmul fast float %922, %938
  %941 = fmul fast float %923, %938
  %942 = call float @dx.op.unary.f32(i32 7, float %910)  ; Saturate(value)
  %943 = call float @dx.op.unary.f32(i32 7, float %911)  ; Saturate(value)
  %944 = call float @dx.op.unary.f32(i32 7, float %912)  ; Saturate(value)
  %945 = call float @dx.op.unary.f32(i32 7, float %942)  ; Saturate(value)
  %946 = call float @dx.op.unary.f32(i32 7, float %943)  ; Saturate(value)
  %947 = call float @dx.op.unary.f32(i32 7, float %944)  ; Saturate(value)
  %948 = fmul fast float %945, %59
  %949 = fmul fast float %946, %59
  %950 = fmul fast float %947, %59
  %951 = fadd fast float %948, %56
  %952 = fadd fast float %949, %57
  %953 = fadd fast float %950, %58
  %954 = call float @dx.op.binary.f32(i32 35, float %939, float 0.000000e+00)  ; FMax(a,b)
  %955 = call float @dx.op.binary.f32(i32 35, float %940, float 0.000000e+00)  ; FMax(a,b)
  %956 = call float @dx.op.binary.f32(i32 35, float %941, float 0.000000e+00)  ; FMax(a,b)
  %957 = call float @dx.op.binary.f32(i32 36, float %954, float 6.500000e+04)  ; FMin(a,b)
  %958 = call float @dx.op.binary.f32(i32 36, float %955, float 6.500000e+04)  ; FMin(a,b)
  %959 = call float @dx.op.binary.f32(i32 36, float %956, float 6.500000e+04)  ; FMin(a,b)
  %960 = fmul fast float %957, %951
  %961 = fmul fast float %958, %952
  %962 = fmul fast float %959, %953
  %963 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %8, i32 19)  ; CBufferLoadLegacy(handle,regIndex)
  %964 = extractvalue %dx.types.CBufRet.f32 %963, 2
  %965 = fdiv fast float %13, %964
  %966 = fadd fast float %965, 0xBFE3333340000000
  %967 = fmul fast float %966, 0x4004000020000000
  %968 = call float @dx.op.unary.f32(i32 7, float %967)  ; Saturate(value)
  %969 = fsub fast float 1.000000e+00, %968
  %970 = fmul fast float %969, %969
  %971 = fmul fast float %970, 0x3F847AE140000000
  %972 = fmul fast float %971, %969
  %973 = mul i32 %14, 44
  %974 = add i32 %973, 26
  %975 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %1, %dx.types.ResourceProperties { i32 12, i32 16 })  ; AnnotateHandle(res,props)  resource: StructuredBuffer<stride=16>
  %976 = call %dx.types.ResRet.f32 @dx.op.rawBufferLoad.f32(i32 139, %dx.types.Handle %975, i32 %974, i32 0, i8 15, i32 4)  ; RawBufferLoad(srv,index,elementOffset,mask,alignment)
  %977 = extractvalue %dx.types.ResRet.f32 %976, 0
  %978 = extractvalue %dx.types.ResRet.f32 %976, 1
  %979 = extractvalue %dx.types.ResRet.f32 %976, 2
  %980 = add i32 %973, 27
  %981 = call %dx.types.ResRet.f32 @dx.op.rawBufferLoad.f32(i32 139, %dx.types.Handle %975, i32 %980, i32 0, i8 15, i32 4)  ; RawBufferLoad(srv,index,elementOffset,mask,alignment)
  %982 = extractvalue %dx.types.ResRet.f32 %981, 0
  %983 = extractvalue %dx.types.ResRet.f32 %981, 1
  %984 = extractvalue %dx.types.ResRet.f32 %981, 2
  %985 = or i32 %973, 1
  %986 = call %dx.types.ResRet.f32 @dx.op.rawBufferLoad.f32(i32 139, %dx.types.Handle %975, i32 %985, i32 0, i8 15, i32 4)  ; RawBufferLoad(srv,index,elementOffset,mask,alignment)
  %987 = extractvalue %dx.types.ResRet.f32 %986, 0
  %988 = extractvalue %dx.types.ResRet.f32 %986, 1
  %989 = extractvalue %dx.types.ResRet.f32 %986, 2
  %990 = add i32 %973, 5
  %991 = call %dx.types.ResRet.f32 @dx.op.rawBufferLoad.f32(i32 139, %dx.types.Handle %975, i32 %990, i32 0, i8 15, i32 4)  ; RawBufferLoad(srv,index,elementOffset,mask,alignment)
  %992 = extractvalue %dx.types.ResRet.f32 %991, 0
  %993 = extractvalue %dx.types.ResRet.f32 %991, 1
  %994 = extractvalue %dx.types.ResRet.f32 %991, 2
  %995 = extractvalue %dx.types.ResRet.f32 %991, 3
  %996 = add i32 %973, 6
  %997 = call %dx.types.ResRet.f32 @dx.op.rawBufferLoad.f32(i32 139, %dx.types.Handle %975, i32 %996, i32 0, i8 15, i32 4)  ; RawBufferLoad(srv,index,elementOffset,mask,alignment)
  %998 = extractvalue %dx.types.ResRet.f32 %997, 0
  %999 = extractvalue %dx.types.ResRet.f32 %997, 1
  %1000 = extractvalue %dx.types.ResRet.f32 %997, 2
  %1001 = extractvalue %dx.types.ResRet.f32 %997, 3
  %1002 = add i32 %973, 7
  %1003 = call %dx.types.ResRet.f32 @dx.op.rawBufferLoad.f32(i32 139, %dx.types.Handle %975, i32 %1002, i32 0, i8 15, i32 4)  ; RawBufferLoad(srv,index,elementOffset,mask,alignment)
  %1004 = extractvalue %dx.types.ResRet.f32 %1003, 0
  %1005 = extractvalue %dx.types.ResRet.f32 %1003, 1
  %1006 = extractvalue %dx.types.ResRet.f32 %1003, 2
  %1007 = extractvalue %dx.types.ResRet.f32 %1003, 3
  %1008 = fsub float -0.000000e+00, %38
  %1009 = fsub float %330, %987
  %1010 = fsub float %331, %988
  %1011 = fsub float %1008, %989
  %1012 = fsub float %1009, %40
  %1013 = fsub float %1010, %41
  %1014 = fsub float %1011, %42
  %1015 = fmul fast float %992, %1012
  %1016 = call float @dx.op.tertiary.f32(i32 46, float %1013, float %993, float %1015)  ; FMad(a,b,c)
  %1017 = call float @dx.op.tertiary.f32(i32 46, float %1014, float %994, float %1016)  ; FMad(a,b,c)
  %1018 = fadd fast float %1017, %995
  %1019 = fmul fast float %998, %1012
  %1020 = call float @dx.op.tertiary.f32(i32 46, float %1013, float %999, float %1019)  ; FMad(a,b,c)
  %1021 = call float @dx.op.tertiary.f32(i32 46, float %1014, float %1000, float %1020)  ; FMad(a,b,c)
  %1022 = fadd fast float %1021, %1001
  %1023 = fmul fast float %1004, %1012
  %1024 = call float @dx.op.tertiary.f32(i32 46, float %1013, float %1005, float %1023)  ; FMad(a,b,c)
  %1025 = call float @dx.op.tertiary.f32(i32 46, float %1014, float %1006, float %1024)  ; FMad(a,b,c)
  %1026 = fadd fast float %1025, %1007
  %1027 = fmul fast float %992, %81
  %1028 = call float @dx.op.tertiary.f32(i32 46, float %82, float %993, float %1027)  ; FMad(a,b,c)
  %1029 = call float @dx.op.tertiary.f32(i32 46, float %83, float %994, float %1028)  ; FMad(a,b,c)
  %1030 = fadd fast float %1018, %1029
  %1031 = fmul fast float %998, %81
  %1032 = call float @dx.op.tertiary.f32(i32 46, float %82, float %999, float %1031)  ; FMad(a,b,c)
  %1033 = call float @dx.op.tertiary.f32(i32 46, float %83, float %1000, float %1032)  ; FMad(a,b,c)
  %1034 = fadd fast float %1022, %1033
  %1035 = fmul fast float %1004, %81
  %1036 = call float @dx.op.tertiary.f32(i32 46, float %82, float %1005, float %1035)  ; FMad(a,b,c)
  %1037 = call float @dx.op.tertiary.f32(i32 46, float %83, float %1006, float %1036)  ; FMad(a,b,c)
  %1038 = fadd fast float %1026, %1037
  %1039 = fcmp fast olt float %1030, %977
  %1040 = fcmp fast olt float %1034, %978
  %1041 = fcmp fast olt float %1038, %979
  %1042 = fcmp fast ogt float %1030, %982
  %1043 = fcmp fast ogt float %1034, %983
  %1044 = fcmp fast ogt float %1038, %984
  %1045 = or i1 %1039, %1042
  %1046 = or i1 %1040, %1043
  %1047 = or i1 %1041, %1044
  %1048 = or i1 %1045, %1046
  %1049 = or i1 %1048, %1047
  %1050 = select i1 %1049, float 0.000000e+00, float 1.000000e+00
  %1051 = fcmp fast ogt float %1050, 0.000000e+00
  %1052 = select i1 %1051, float 1.000000e+00, float -1.000000e+00
  %1053 = fcmp fast olt float %1052, 0.000000e+00
  call void @dx.op.discard(i32 82, i1 %1053)  ; Discard(condition)
  %1054 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %7, i32 5)  ; CBufferLoadLegacy(handle,regIndex)
  %1055 = extractvalue %dx.types.CBufRet.f32 %1054, 0
  %1056 = fmul fast float %44, 2.097152e+06
  %1057 = fmul fast float %45, 2.097152e+06
  %1058 = fmul fast float %46, 2.097152e+06
  %1059 = fadd fast float %84, %1056
  %1060 = fadd fast float %85, %1057
  %1061 = fadd fast float %86, %1058
  %1062 = fptosi float %1055 to i32
  %1063 = icmp eq i32 %1062, -1
  %1064 = call %dx.types.CBufRet.i32 @dx.op.cbufferLoadLegacy.i32(i32 59, %dx.types.Handle %9, i32 555)  ; CBufferLoadLegacy(handle,regIndex)
  %1065 = extractvalue %dx.types.CBufRet.i32 %1064, 1
  %1066 = icmp eq i32 %1065, 0
  %1067 = or i1 %1063, %1066
  br i1 %1067, label %1161, label %1068

; <label>:1068                                    ; preds = %882
  %1069 = extractvalue %dx.types.CBufRet.f32 %1054, 1
  %1070 = add nsw i32 %1065, -1
  %1071 = call i32 @dx.op.binary.i32(i32 37, i32 %1062, i32 0)  ; IMax(a,b)
  %1072 = call i32 @dx.op.binary.i32(i32 38, i32 %1071, i32 %1070)  ; IMin(a,b)
  %1073 = add i32 %1072, 437
  %1074 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %9, i32 %1073)  ; CBufferLoadLegacy(handle,regIndex)
  %1075 = extractvalue %dx.types.CBufRet.f32 %1074, 0
  %1076 = extractvalue %dx.types.CBufRet.f32 %1074, 1
  %1077 = extractvalue %dx.types.CBufRet.f32 %1074, 2
  %1078 = shl i32 %1072, 2
  %1079 = add i32 %1078, 373
  %1080 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %9, i32 %1079)  ; CBufferLoadLegacy(handle,regIndex)
  %1081 = extractvalue %dx.types.CBufRet.f32 %1080, 0
  %1082 = extractvalue %dx.types.CBufRet.f32 %1080, 1
  %1083 = extractvalue %dx.types.CBufRet.f32 %1080, 2
  %1084 = extractvalue %dx.types.CBufRet.f32 %1080, 3
  %1085 = add i32 %1078, 374
  %1086 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %9, i32 %1085)  ; CBufferLoadLegacy(handle,regIndex)
  %1087 = extractvalue %dx.types.CBufRet.f32 %1086, 0
  %1088 = extractvalue %dx.types.CBufRet.f32 %1086, 1
  %1089 = extractvalue %dx.types.CBufRet.f32 %1086, 2
  %1090 = extractvalue %dx.types.CBufRet.f32 %1086, 3
  %1091 = add i32 %1078, 375
  %1092 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %9, i32 %1091)  ; CBufferLoadLegacy(handle,regIndex)
  %1093 = extractvalue %dx.types.CBufRet.f32 %1092, 0
  %1094 = extractvalue %dx.types.CBufRet.f32 %1092, 1
  %1095 = extractvalue %dx.types.CBufRet.f32 %1092, 2
  %1096 = extractvalue %dx.types.CBufRet.f32 %1092, 3
  %1097 = add i32 %1078, 376
  %1098 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %9, i32 %1097)  ; CBufferLoadLegacy(handle,regIndex)
  %1099 = extractvalue %dx.types.CBufRet.f32 %1098, 0
  %1100 = extractvalue %dx.types.CBufRet.f32 %1098, 1
  %1101 = extractvalue %dx.types.CBufRet.f32 %1098, 2
  %1102 = extractvalue %dx.types.CBufRet.f32 %1098, 3
  %1103 = fmul fast float %1081, %1059
  %1104 = call float @dx.op.tertiary.f32(i32 46, float %1060, float %1087, float %1103)  ; FMad(a,b,c)
  %1105 = call float @dx.op.tertiary.f32(i32 46, float %1061, float %1093, float %1104)  ; FMad(a,b,c)
  %1106 = fadd fast float %1105, %1099
  %1107 = fmul fast float %1082, %1059
  %1108 = call float @dx.op.tertiary.f32(i32 46, float %1060, float %1088, float %1107)  ; FMad(a,b,c)
  %1109 = call float @dx.op.tertiary.f32(i32 46, float %1061, float %1094, float %1108)  ; FMad(a,b,c)
  %1110 = fadd fast float %1109, %1100
  %1111 = fmul fast float %1083, %1059
  %1112 = call float @dx.op.tertiary.f32(i32 46, float %1060, float %1089, float %1111)  ; FMad(a,b,c)
  %1113 = call float @dx.op.tertiary.f32(i32 46, float %1061, float %1095, float %1112)  ; FMad(a,b,c)
  %1114 = fadd fast float %1113, %1101
  %1115 = fmul fast float %1084, %1059
  %1116 = call float @dx.op.tertiary.f32(i32 46, float %1060, float %1090, float %1115)  ; FMad(a,b,c)
  %1117 = call float @dx.op.tertiary.f32(i32 46, float %1061, float %1096, float %1116)  ; FMad(a,b,c)
  %1118 = fadd fast float %1117, %1102
  %1119 = fdiv fast float %1106, %1118
  %1120 = fdiv fast float %1110, %1118
  %1121 = fdiv fast float %1114, %1118
  %1122 = fcmp fast oeq float %1069, 1.000000e+00
  %1123 = call float @dx.op.unary.f32(i32 6, float %1119)  ; FAbs(value)
  %1124 = call float @dx.op.unary.f32(i32 6, float %1120)  ; FAbs(value)
  %1125 = call float @dx.op.unary.f32(i32 6, float %1121)  ; FAbs(value)
  %1126 = fcmp fast ole float %1123, %1075
  %1127 = fcmp fast ole float %1124, %1076
  %1128 = fcmp fast ole float %1125, %1077
  %1129 = and i1 %1126, %1127
  %1130 = and i1 %1129, %1128
  %1131 = and i1 %1122, %1130
  br i1 %1131, label %1132, label %1139

; <label>:1132                                    ; preds = %1068
  %1133 = fsub fast float %1077, %1125
  %1134 = add i32 %1072, 453
  %1135 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %9, i32 %1134)  ; CBufferLoadLegacy(handle,regIndex)
  %1136 = extractvalue %dx.types.CBufRet.f32 %1135, 1
  %1137 = fmul fast float %1136, %1133
  %1138 = call float @dx.op.unary.f32(i32 7, float %1137)  ; Saturate(value)
  br label %1139

; <label>:1139                                    ; preds = %1132, %1068
  %1140 = phi float [ 0.000000e+00, %1132 ], [ %1121, %1068 ]
  %1141 = phi float [ %1138, %1132 ], [ 1.000000e+00, %1068 ]
  %1142 = call float @dx.op.unary.f32(i32 6, float %1140)  ; FAbs(value)
  %1143 = fcmp fast ole float %1142, %1077
  %1144 = and i1 %1129, %1143
  br i1 %1144, label %1145, label %1152

; <label>:1145                                    ; preds = %1139
  %1146 = fsub fast float %1075, %1123
  %1147 = fsub fast float %1076, %1124
  %1148 = fsub fast float %1077, %1142
  %1149 = call float @dx.op.binary.f32(i32 36, float %1147, float %1148)  ; FMin(a,b)
  %1150 = call float @dx.op.binary.f32(i32 36, float %1146, float %1149)  ; FMin(a,b)
  %1151 = fmul fast float %1150, %1150
  br label %1152

; <label>:1152                                    ; preds = %1145, %1139
  %1153 = phi float [ %1151, %1145 ], [ 0.000000e+00, %1139 ]
  %1154 = call float @dx.op.unary.f32(i32 24, float %1153)  ; Sqrt(value)
  %1155 = add i32 %1072, 453
  %1156 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %9, i32 %1155)  ; CBufferLoadLegacy(handle,regIndex)
  %1157 = extractvalue %dx.types.CBufRet.f32 %1156, 0
  %1158 = fmul fast float %1157, %1154
  %1159 = call float @dx.op.unary.f32(i32 7, float %1158)  ; Saturate(value)
  %1160 = call float @dx.op.binary.f32(i32 36, float %1159, float %1141)  ; FMin(a,b)
  br label %1161

; <label>:1161                                    ; preds = %1152, %882
  %1162 = phi float [ %1160, %1152 ], [ 1.000000e+00, %882 ]
  %1163 = extractvalue %dx.types.CBufRet.f32 %1054, 2
  %1164 = fmul fast float %1163, %1162
  %1165 = fmul fast float %960, %972
  %1166 = fmul fast float %961, %972
  %1167 = fmul fast float %962, %972
  call void @dx.op.storeOutput.f32(i32 5, i32 0, i32 0, i8 0, float %1165)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 0, i32 0, i8 1, float %1166)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 0, i32 0, i8 2, float %1167)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 0, i32 0, i8 3, float %1164)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  %1168 = fmul fast float %972, %957
  %1169 = fmul fast float %972, %958
  %1170 = fmul fast float %972, %959
  call void @dx.op.storeOutput.f32(i32 5, i32 1, i32 0, i8 0, float %1168)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 1, i32 0, i8 1, float %1169)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 1, i32 0, i8 2, float %1170)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 1, i32 0, i8 3, float %1164)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 2, i32 0, i8 0, float 0.000000e+00)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 2, i32 0, i8 1, float 0.000000e+00)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 2, i32 0, i8 2, float 0.000000e+00)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 2, i32 0, i8 3, float %1164)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
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

; Function Attrs: nounwind
declare void @dx.op.discard(i32, i1) #1

; Function Attrs: nounwind readnone
declare float @dx.op.binary.f32(i32, float, float) #0

; Function Attrs: nounwind readnone
declare float @dx.op.tertiary.f32(i32, float, float, float) #0

; Function Attrs: nounwind readonly
declare %dx.types.ResRet.f32 @dx.op.rawBufferLoad.f32(i32, %dx.types.Handle, i32, i32, i8, i32) #2

; Function Attrs: nounwind readonly
declare %dx.types.ResRet.f32 @dx.op.bufferLoad.f32(i32, %dx.types.Handle, i32, i32) #2

; Function Attrs: nounwind readnone
declare i32 @dx.op.binary.i32(i32, i32, i32) #0

; Function Attrs: nounwind readnone
declare float @dx.op.dot2.f32(i32, float, float, float, float) #0

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
!dx.viewIdState = !{!15}
!dx.entryPoints = !{!16}

!0 = !{!"dxc(private) 1.8.0.0 (private, 00000000)"}
!1 = !{i32 1, i32 6}
!2 = !{i32 1, i32 8}
!3 = !{!"ps", i32 6, i32 6}
!4 = !{!5, null, !11, null}
!5 = !{!6, !8, !9}
!6 = !{i32 0, %"class.Buffer<vector<float, 4> >"* undef, !"", i32 0, i32 0, i32 1, i32 10, i32 0, !7}
!7 = !{i32 0, i32 9}
!8 = !{i32 1, %"class.Buffer<vector<float, 4> >"* undef, !"", i32 0, i32 1, i32 1, i32 10, i32 0, !7}
!9 = !{i32 2, %"class.StructuredBuffer<vector<float, 4> >"* undef, !"", i32 0, i32 2, i32 1, i32 12, i32 0, !10}
!10 = !{i32 1, i32 16}
!11 = !{!12, !13, !14}
!12 = !{i32 0, %hostlayout.View* undef, !"", i32 0, i32 0, i32 1, i32 10076, null}
!13 = !{i32 1, %hostlayout.VoxelizeVolumePass* undef, !"", i32 0, i32 1, i32 1, i32 392, null}
!14 = !{i32 2, %Material* undef, !"", i32 0, i32 2, i32 1, i32 108, null}
!15 = !{[18 x i32] [i32 16, i32 12, i32 0, i32 0, i32 0, i32 0, i32 0, i32 0, i32 0, i32 0, i32 0, i32 0, i32 0, i32 0, i32 2303, i32 2303, i32 2303, i32 119]}
!16 = !{void ()* @VoxelizePS, !"VoxelizePS", !17, !4, !33}
!17 = !{!18, !27, null}
!18 = !{!19, !21, !22, !24, !26}
!19 = !{i32 0, !"TEXCOORD10_centroid", i8 9, i8 0, !20, i8 2, i32 1, i8 4, i32 0, i8 0, null}
!20 = !{i32 0}
!21 = !{i32 1, !"TEXCOORD11_centroid", i8 9, i8 0, !20, i8 2, i32 1, i8 4, i32 1, i8 0, null}
!22 = !{i32 2, !"PRIMITIVE_ID", i8 5, i8 0, !20, i8 1, i32 1, i8 1, i32 2, i8 0, !23}
!23 = !{i32 3, i32 1}
!24 = !{i32 3, !"SV_Position", i8 9, i8 3, !20, i8 4, i32 1, i8 4, i32 3, i8 0, !25}
!25 = !{i32 3, i32 15}
!26 = !{i32 4, !"SV_RenderTargetArrayIndex", i8 5, i8 4, !20, i8 1, i32 1, i8 1, i32 2, i8 1, null}
!27 = !{!28, !29, !31}
!28 = !{i32 0, !"SV_Target", i8 9, i8 16, !20, i8 0, i32 1, i8 4, i32 0, i8 0, !25}
!29 = !{i32 1, !"SV_Target", i8 9, i8 16, !30, i8 0, i32 1, i8 4, i32 1, i8 0, !25}
!30 = !{i32 1}
!31 = !{i32 2, !"SV_Target", i8 9, i8 16, !32, i8 0, i32 1, i8 4, i32 2, i8 0, !25}
!32 = !{i32 2}
!33 = !{i32 0, i64 16, i32 5, !20}
