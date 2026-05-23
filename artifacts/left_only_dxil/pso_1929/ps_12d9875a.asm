;
; Input signature:
;
; Name                 Index   Mask Register SysValue  Format   Used
; -------------------- ----- ------ -------- -------- ------- ------
; TEXCOORD10_centroid      0   xyzw        0     NONE   float       
; TEXCOORD11_centroid      0   xyzw        1     NONE   float       
; TEXCOORD                 0   xyzw        2     NONE   float   xy  
; PRIMITIVE_ID             0   x           3     NONE    uint   x   
; SV_IsFrontFace           0    y          3    FFACE    uint       
; TEXCOORD                 9   xyz         4     NONE   float   xyz 
; SV_Position              0   xyzw        5      POS   float   xyz 
;
;
; Output signature:
;
; Name                 Index   Mask Register SysValue  Format   Used
; -------------------- ----- ------ -------- -------- ------- ------
; SV_Target                0   xyzw        0   TARGET   float   xyzw
; SV_Target                1   xyzw        1   TARGET   float   xyzw
; SV_Target                2   xyzw        2   TARGET   float   xyzw
; SV_Target                3   xyzw        3   TARGET   float   xyzw
;
; shader debug name: 9b61f6981636538d6b2d911ece85113d.pdb
; shader hash: 9b61f6981636538d6b2d911ece85113d
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
; SigOutputElements: 4
; SigPatchConstOrPrimElements: 0
; SigInputVectors: 6
; SigOutputVectors[0]: 4
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
; TEXCOORD                 0                 linear       
; PRIMITIVE_ID             0        nointerpolation       
; TEXCOORD                 9                 linear       
; SV_Position              0          noperspective       
; SV_IsFrontFace           0        nointerpolation       
;
; Output signature:
;
; Name                 Index             InterpMode DynIdx
; -------------------- ----- ---------------------- ------
; SV_Target                0                              
; SV_Target                1                              
; SV_Target                2                              
; SV_Target                3                              
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
;   [76 x i8] (type annotation not present)
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
;                                   sampler      NA          NA      S0             s0     1
;                                   sampler      NA          NA      S1             s1     1
;                                   texture  struct         r/o      T0             t0     1
;                                   texture     f32          2d      T1             t1     1
;                                   texture     f32          2d      T2             t2     1
;
;
; ViewId state:
;
; Number of inputs: 24, outputs: 16
; Outputs dependent on ViewId: {  }
; Inputs contributing to computation of Outputs:
;   output 0 depends on inputs: { 8, 9, 12, 16, 17, 18, 20, 21, 22 }
;   output 1 depends on inputs: { 8, 9, 12, 20, 21, 22 }
;   output 2 depends on inputs: { 8, 9, 12, 16, 17, 18, 20, 21, 22 }
;
target datalayout = "e-m:e-p:32:32-i1:32-i8:32-i16:32-i32:32-i64:64-f16:32-f32:32-f64:64-n8:16:32:64"
target triple = "dxil-ms-dx"

%dx.types.Handle = type { i8* }
%dx.types.ResBind = type { i32, i32, i32, i8 }
%dx.types.ResourceProperties = type { i32, i32 }
%dx.types.CBufRet.f32 = type { float, float, float, float }
%dx.types.ResRet.f32 = type { float, float, float, float, i32 }
%"class.StructuredBuffer<vector<float, 4> >" = type { <4 x float> }
%"class.Texture2D<vector<float, 4> >" = type { <4 x float>, %"class.Texture2D<vector<float, 4> >::mips_type" }
%"class.Texture2D<vector<float, 4> >::mips_type" = type { i32 }
%hostlayout.View = type { [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <4 x float>, <4 x float>, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], <4 x float>, <4 x float>, <2 x float>, <2 x float>, <2 x float>, <2 x float>, <4 x float>, <4 x float>, <4 x i32>, <4 x float>, <4 x float>, <4 x float>, <4 x float>, <2 x float>, <2 x float>, i32, float, float, float, <4 x float>, <4 x float>, <4 x float>, <2 x float>, <2 x float>, float, float, float, float, <3 x float>, float, float, float, float, float, float, float, float, i32, i32, i32, i32, i32, i32, i32, i32, float, float, float, float, float, <4 x float>, <3 x float>, float, [2 x <4 x float>], [2 x <4 x float>], <4 x float>, <4 x float>, float, float, float, float, float, float, float, float, float, float, float, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, [2 x <4 x float>], [2 x <4 x float>], [2 x <4 x float>], [2 x <4 x float>], [2 x <4 x float>], <4 x float>, <3 x float>, float, <4 x float>, [4 x <4 x float>], <4 x float>, float, float, float, float, <4 x float>, float, float, float, float, float, float, float, float, <3 x float>, float, float, float, float, float, <4 x float>, float, float, float, float, <4 x float>, float, float, float, float, [8 x <4 x float>], float, float, float, float, i32, float, float, float, <3 x float>, i32, [6 x <4 x float>], [6 x <4 x float>], [6 x <4 x float>], [6 x <4 x float>], float, float, i32, i32, <3 x float>, float, <3 x float>, float, float, float, i32, float, float, float, float, float, float, float, <2 x i32>, float, float, float, float, <3 x float>, float, <3 x float>, float, <2 x float>, <2 x float>, <2 x float>, <2 x float>, <2 x float>, float, float, <3 x float>, float, <2 x float>, <2 x float>, float, float, float, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, float, float, float, float, [2 x <4 x float>], <4 x float>, i32, i32, i32, i32, i32, i32, i32, float, <4 x float>, i32, i32, i32, i32, <4 x i32>, <2 x float>, float, float, <4 x float>, <4 x float>, <4 x float>, <3 x float>, float, i32, i32, i32, i32, [32 x <4 x i32>], i32, float, float, float, <4 x float>, <4 x float>, <4 x float>, <4 x float>, <2 x float>, float, float, <4 x float>, <4 x float>, <4 x float>, float, float, float, i32, <4 x i32>, i32, i32, i32, i32, [4 x <4 x float>], <3 x i32>, i32, <3 x float>, float, <3 x float>, float, <3 x i32>, i32, [16 x <4 x float>], float, float, float, float, float, float, float, float, [4 x [4 x <4 x float>]], [4 x <4 x float>], [4 x <4 x float>], [16 x [4 x <4 x float>]], [16 x <4 x float>], [16 x <4 x float>], [8 x [4 x <4 x float>]], [8 x <4 x float>], [8 x <4 x float>], [8 x <4 x float>], [8 x <4 x float>], [2 x <4 x float>], [2 x <4 x float>], [2 x <4 x float>], [2 x <4 x float>], [2 x <4 x float>], [2 x <4 x float>], [2 x <4 x float>], [2 x <4 x float>], [2 x <4 x float>], [2 x <4 x float>], <4 x float>, <4 x float>, float, i32, i32, float, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, <3 x float>, i32, <4 x float>, <4 x float>, <4 x float>, <3 x float>, i32, <3 x i32>, i32, <3 x i32>, i32, <3 x i32>, i32, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, <4 x float>, float, float, i32, i32, i32, i32, i32, i32, <4 x float>, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, <3 x i32>, i32, <3 x i32>, i32, <3 x float>, float, <3 x float> }
%Material = type { [2 x <4 x float>], i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32 }
%struct.SamplerState = type { i32 }

define void @MainPS() {
  %1 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 2, i32 2, i32 0, i8 0 }, i32 2, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %2 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 1, i32 1, i32 0, i8 0 }, i32 1, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %3 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind zeroinitializer, i32 0, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %4 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 1, i32 1, i32 0, i8 3 }, i32 1, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %5 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 0, i32 0, i32 0, i8 3 }, i32 0, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %6 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 1, i32 1, i32 0, i8 2 }, i32 1, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %7 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 0, i32 0, i32 0, i8 2 }, i32 0, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %8 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %6, %dx.types.ResourceProperties { i32 13, i32 76 })  ; AnnotateHandle(res,props)  resource: CBuffer
  %9 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %7, %dx.types.ResourceProperties { i32 13, i32 10076 })  ; AnnotateHandle(res,props)  resource: CBuffer
  %10 = call float @dx.op.loadInput.f32(i32 4, i32 5, i32 0, i8 0, i32 undef), !dx.precise !37  ; LoadInput(inputSigId,rowIndex,colIndex,gsVertexAxis)
  %11 = call float @dx.op.loadInput.f32(i32 4, i32 5, i32 0, i8 1, i32 undef), !dx.precise !37  ; LoadInput(inputSigId,rowIndex,colIndex,gsVertexAxis)
  %12 = call float @dx.op.loadInput.f32(i32 4, i32 5, i32 0, i8 2, i32 undef), !dx.precise !37  ; LoadInput(inputSigId,rowIndex,colIndex,gsVertexAxis)
  %13 = call float @dx.op.loadInput.f32(i32 4, i32 4, i32 0, i8 0, i32 undef), !dx.precise !37  ; LoadInput(inputSigId,rowIndex,colIndex,gsVertexAxis)
  %14 = call float @dx.op.loadInput.f32(i32 4, i32 4, i32 0, i8 1, i32 undef), !dx.precise !37  ; LoadInput(inputSigId,rowIndex,colIndex,gsVertexAxis)
  %15 = call float @dx.op.loadInput.f32(i32 4, i32 4, i32 0, i8 2, i32 undef), !dx.precise !37  ; LoadInput(inputSigId,rowIndex,colIndex,gsVertexAxis)
  %16 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %9, i32 44)  ; CBufferLoadLegacy(handle,regIndex)
  %17 = extractvalue %dx.types.CBufRet.f32 %16, 0
  %18 = extractvalue %dx.types.CBufRet.f32 %16, 1
  %19 = extractvalue %dx.types.CBufRet.f32 %16, 2
  %20 = extractvalue %dx.types.CBufRet.f32 %16, 3
  %21 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %9, i32 45)  ; CBufferLoadLegacy(handle,regIndex)
  %22 = extractvalue %dx.types.CBufRet.f32 %21, 0
  %23 = extractvalue %dx.types.CBufRet.f32 %21, 1
  %24 = extractvalue %dx.types.CBufRet.f32 %21, 2
  %25 = extractvalue %dx.types.CBufRet.f32 %21, 3
  %26 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %9, i32 46)  ; CBufferLoadLegacy(handle,regIndex)
  %27 = extractvalue %dx.types.CBufRet.f32 %26, 0
  %28 = extractvalue %dx.types.CBufRet.f32 %26, 1
  %29 = extractvalue %dx.types.CBufRet.f32 %26, 2
  %30 = extractvalue %dx.types.CBufRet.f32 %26, 3
  %31 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %9, i32 47)  ; CBufferLoadLegacy(handle,regIndex)
  %32 = extractvalue %dx.types.CBufRet.f32 %31, 0
  %33 = extractvalue %dx.types.CBufRet.f32 %31, 1
  %34 = extractvalue %dx.types.CBufRet.f32 %31, 2
  %35 = extractvalue %dx.types.CBufRet.f32 %31, 3
  %36 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %9, i32 121)  ; CBufferLoadLegacy(handle,regIndex)
  %37 = extractvalue %dx.types.CBufRet.f32 %36, 0
  %38 = extractvalue %dx.types.CBufRet.f32 %36, 1
  %39 = extractvalue %dx.types.CBufRet.f32 %36, 2
  %40 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %9, i32 124)  ; CBufferLoadLegacy(handle,regIndex)
  %41 = extractvalue %dx.types.CBufRet.f32 %40, 0
  %42 = extractvalue %dx.types.CBufRet.f32 %40, 1
  %43 = extractvalue %dx.types.CBufRet.f32 %40, 2
  %44 = fsub float -0.000000e+00, %37
  %45 = fsub float -0.000000e+00, %38
  %46 = fsub float -0.000000e+00, %39
  %47 = call float @dx.op.loadInput.f32(i32 4, i32 2, i32 0, i8 0, i32 undef)  ; LoadInput(inputSigId,rowIndex,colIndex,gsVertexAxis)
  %48 = call float @dx.op.loadInput.f32(i32 4, i32 2, i32 0, i8 1, i32 undef)  ; LoadInput(inputSigId,rowIndex,colIndex,gsVertexAxis)
  %49 = fmul float %10, %17
  %50 = call float @dx.op.tertiary.f32(i32 46, float %11, float %22, float %49), !dx.precise !37  ; FMad(a,b,c)
  %51 = call float @dx.op.tertiary.f32(i32 46, float %12, float %27, float %50), !dx.precise !37  ; FMad(a,b,c)
  %52 = call float @dx.op.tertiary.f32(i32 46, float 1.000000e+00, float %32, float %51), !dx.precise !37  ; FMad(a,b,c)
  %53 = fmul float %10, %18
  %54 = call float @dx.op.tertiary.f32(i32 46, float %11, float %23, float %53), !dx.precise !37  ; FMad(a,b,c)
  %55 = call float @dx.op.tertiary.f32(i32 46, float %12, float %28, float %54), !dx.precise !37  ; FMad(a,b,c)
  %56 = call float @dx.op.tertiary.f32(i32 46, float 1.000000e+00, float %33, float %55), !dx.precise !37  ; FMad(a,b,c)
  %57 = fmul float %10, %19
  %58 = call float @dx.op.tertiary.f32(i32 46, float %11, float %24, float %57), !dx.precise !37  ; FMad(a,b,c)
  %59 = call float @dx.op.tertiary.f32(i32 46, float %12, float %29, float %58), !dx.precise !37  ; FMad(a,b,c)
  %60 = call float @dx.op.tertiary.f32(i32 46, float 1.000000e+00, float %34, float %59), !dx.precise !37  ; FMad(a,b,c)
  %61 = fmul float %10, %20
  %62 = call float @dx.op.tertiary.f32(i32 46, float %11, float %25, float %61), !dx.precise !37  ; FMad(a,b,c)
  %63 = call float @dx.op.tertiary.f32(i32 46, float %12, float %30, float %62), !dx.precise !37  ; FMad(a,b,c)
  %64 = call float @dx.op.tertiary.f32(i32 46, float 1.000000e+00, float %35, float %63), !dx.precise !37  ; FMad(a,b,c)
  %65 = fdiv float %52, %64
  %66 = fdiv float %56, %64
  %67 = fdiv float %60, %64
  %68 = fsub float %65, %41
  %69 = fsub float %66, %42
  %70 = fsub float %67, %43
  %71 = call float @dx.op.tertiary.f32(i32 46, float %37, float 2.097152e+06, float %68), !dx.precise !37  ; FMad(a,b,c)
  %72 = call float @dx.op.tertiary.f32(i32 46, float %38, float 2.097152e+06, float %69), !dx.precise !37  ; FMad(a,b,c)
  %73 = call float @dx.op.tertiary.f32(i32 46, float %39, float 2.097152e+06, float %70), !dx.precise !37  ; FMad(a,b,c)
  %74 = call float @dx.op.tertiary.f32(i32 46, float %44, float 2.097152e+06, float %71), !dx.precise !37  ; FMad(a,b,c)
  %75 = call float @dx.op.tertiary.f32(i32 46, float %45, float 2.097152e+06, float %72), !dx.precise !37  ; FMad(a,b,c)
  %76 = call float @dx.op.tertiary.f32(i32 46, float %46, float 2.097152e+06, float %73), !dx.precise !37  ; FMad(a,b,c)
  %77 = fsub float %68, %74
  %78 = fsub float %69, %75
  %79 = fsub float %70, %76
  %80 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %9, i32 163)  ; CBufferLoadLegacy(handle,regIndex)
  %81 = extractvalue %dx.types.CBufRet.f32 %80, 2
  %82 = fadd fast float %81, 1.562500e-02
  %83 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %8, i32 0)  ; CBufferLoadLegacy(handle,regIndex)
  %84 = extractvalue %dx.types.CBufRet.f32 %83, 0
  %85 = fmul fast float %82, %84
  %86 = call float @dx.op.unary.f32(i32 22, float %85)  ; Frc(value)
  %87 = call float @dx.op.unary.f32(i32 22, float %86)  ; Frc(value)
  %88 = call float @dx.op.binary.f32(i32 36, float %87, float 0x3FEFFFFDE0000000)  ; FMin(a,b)
  %89 = fmul fast float %88, 6.400000e+01
  %90 = call float @dx.op.unary.f32(i32 27, float %89)  ; Round_ni(value)
  %91 = fmul fast float %90, 1.250000e-01
  %92 = call float @dx.op.unary.f32(i32 27, float %91)  ; Round_ni(value)
  %93 = fadd fast float %90, %47
  %94 = fmul fast float %93, 1.250000e-01
  %95 = fadd fast float %92, %48
  %96 = fmul fast float %95, 1.250000e-01
  %97 = call float @dx.op.unary.f32(i32 22, float %89)  ; Frc(value)
  %98 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %2, %dx.types.ResourceProperties { i32 2, i32 1033 })  ; AnnotateHandle(res,props)  resource: Texture2D<4xF32>
  %99 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %5, %dx.types.ResourceProperties { i32 14, i32 0 })  ; AnnotateHandle(res,props)  resource: SamplerState
  %100 = call %dx.types.ResRet.f32 @dx.op.sample.f32(i32 60, %dx.types.Handle %98, %dx.types.Handle %99, float %94, float %96, float undef, float undef, i32 0, i32 0, i32 undef, float undef)  ; Sample(srv,sampler,coord0,coord1,coord2,coord3,offset0,offset1,offset2,clamp)
  %101 = extractvalue %dx.types.ResRet.f32 %100, 0
  %102 = extractvalue %dx.types.ResRet.f32 %100, 1
  %103 = fadd fast float %101, -5.000000e-01
  %104 = fadd fast float %102, -5.000000e-01
  %105 = fadd fast float %90, 1.000000e+00
  %106 = fmul fast float %105, 1.250000e-01
  %107 = call float @dx.op.unary.f32(i32 27, float %106)  ; Round_ni(value)
  %108 = fadd fast float %105, %47
  %109 = fmul fast float %108, 1.250000e-01
  %110 = fadd fast float %107, %48
  %111 = fmul fast float %110, 1.250000e-01
  %112 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %2, %dx.types.ResourceProperties { i32 2, i32 1033 })  ; AnnotateHandle(res,props)  resource: Texture2D<4xF32>
  %113 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %5, %dx.types.ResourceProperties { i32 14, i32 0 })  ; AnnotateHandle(res,props)  resource: SamplerState
  %114 = call %dx.types.ResRet.f32 @dx.op.sample.f32(i32 60, %dx.types.Handle %112, %dx.types.Handle %113, float %109, float %111, float undef, float undef, i32 0, i32 0, i32 undef, float undef)  ; Sample(srv,sampler,coord0,coord1,coord2,coord3,offset0,offset1,offset2,clamp)
  %115 = extractvalue %dx.types.ResRet.f32 %114, 0
  %116 = extractvalue %dx.types.ResRet.f32 %114, 1
  %117 = fadd fast float %115, -5.000000e-01
  %118 = fadd fast float %116, -5.000000e-01
  %119 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %8, i32 0)  ; CBufferLoadLegacy(handle,regIndex)
  %120 = extractvalue %dx.types.CBufRet.f32 %119, 1
  %121 = fsub fast float %117, %103
  %122 = fsub fast float %118, %104
  %123 = fmul fast float %121, %120
  %124 = fmul fast float %122, %120
  %125 = fadd fast float %123, %103
  %126 = fadd fast float %124, %104
  %127 = extractvalue %dx.types.CBufRet.f32 %119, 2
  %128 = fmul fast float %97, 2.500000e-01
  %129 = fmul fast float %128, %127
  %130 = fmul fast float %129, %125
  %131 = fmul fast float %129, %126
  %132 = fsub fast float %94, %130
  %133 = fadd fast float %131, %96
  %134 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %1, %dx.types.ResourceProperties { i32 2, i32 1033 })  ; AnnotateHandle(res,props)  resource: Texture2D<4xF32>
  %135 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %4, %dx.types.ResourceProperties { i32 14, i32 0 })  ; AnnotateHandle(res,props)  resource: SamplerState
  %136 = call %dx.types.ResRet.f32 @dx.op.sample.f32(i32 60, %dx.types.Handle %134, %dx.types.Handle %135, float %132, float %133, float undef, float undef, i32 0, i32 0, i32 undef, float undef)  ; Sample(srv,sampler,coord0,coord1,coord2,coord3,offset0,offset1,offset2,clamp)
  %137 = extractvalue %dx.types.ResRet.f32 %136, 0
  %138 = extractvalue %dx.types.ResRet.f32 %136, 1
  %139 = extractvalue %dx.types.ResRet.f32 %136, 2
  %140 = fsub fast float 1.000000e+00, %97
  %141 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %8, i32 0)  ; CBufferLoadLegacy(handle,regIndex)
  %142 = extractvalue %dx.types.CBufRet.f32 %141, 1
  %143 = fsub fast float %103, %117
  %144 = fsub fast float %104, %118
  %145 = fmul fast float %142, %143
  %146 = fmul fast float %142, %144
  %147 = fadd fast float %145, %117
  %148 = fadd fast float %146, %118
  %149 = extractvalue %dx.types.CBufRet.f32 %141, 2
  %150 = fmul fast float %140, 2.500000e-01
  %151 = fmul fast float %150, %149
  %152 = fmul fast float %151, %147
  %153 = fmul fast float %151, %148
  %154 = fadd fast float %152, %109
  %155 = fsub fast float %111, %153
  %156 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %1, %dx.types.ResourceProperties { i32 2, i32 1033 })  ; AnnotateHandle(res,props)  resource: Texture2D<4xF32>
  %157 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %4, %dx.types.ResourceProperties { i32 14, i32 0 })  ; AnnotateHandle(res,props)  resource: SamplerState
  %158 = call %dx.types.ResRet.f32 @dx.op.sample.f32(i32 60, %dx.types.Handle %156, %dx.types.Handle %157, float %154, float %155, float undef, float undef, i32 0, i32 0, i32 undef, float undef)  ; Sample(srv,sampler,coord0,coord1,coord2,coord3,offset0,offset1,offset2,clamp)
  %159 = extractvalue %dx.types.ResRet.f32 %158, 0
  %160 = extractvalue %dx.types.ResRet.f32 %158, 1
  %161 = extractvalue %dx.types.ResRet.f32 %158, 2
  %162 = fsub fast float %159, %137
  %163 = fsub fast float %160, %138
  %164 = fsub fast float %161, %139
  %165 = fmul fast float %162, %97
  %166 = fmul fast float %163, %97
  %167 = fmul fast float %164, %97
  %168 = fadd fast float %165, %137
  %169 = fadd fast float %166, %138
  %170 = fadd fast float %167, %139
  %171 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %8, i32 0)  ; CBufferLoadLegacy(handle,regIndex)
  %172 = extractvalue %dx.types.CBufRet.f32 %171, 3
  %173 = fmul fast float %168, %172
  %174 = fmul fast float %169, %172
  %175 = fmul fast float %170, %172
  %176 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %8, i32 1)  ; CBufferLoadLegacy(handle,regIndex)
  %177 = extractvalue %dx.types.CBufRet.f32 %176, 0
  %178 = extractvalue %dx.types.CBufRet.f32 %176, 1
  %179 = extractvalue %dx.types.CBufRet.f32 %176, 2
  %180 = extractvalue %dx.types.CBufRet.f32 %176, 3
  %181 = fsub fast float %178, %173
  %182 = fsub fast float %179, %174
  %183 = fsub fast float %180, %175
  %184 = fmul fast float %181, %177
  %185 = fmul fast float %182, %177
  %186 = fmul fast float %183, %177
  %187 = fadd fast float %184, %173
  %188 = fadd fast float %185, %174
  %189 = fadd fast float %186, %175
  %190 = call float @dx.op.binary.f32(i32 35, float %187, float 0.000000e+00)  ; FMax(a,b)
  %191 = call float @dx.op.binary.f32(i32 35, float %188, float 0.000000e+00)  ; FMax(a,b)
  %192 = call float @dx.op.binary.f32(i32 35, float %189, float 0.000000e+00)  ; FMax(a,b)
  %193 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %9, i32 161)  ; CBufferLoadLegacy(handle,regIndex)
  %194 = extractvalue %dx.types.CBufRet.f32 %193, 2
  %195 = fcmp ogt float %194, 0.000000e+00
  br i1 %195, label %196, label %297, !dx.controlflow.hints !43

; <label>:196                                     ; preds = %0
  %197 = call i32 @dx.op.loadInput.i32(i32 4, i32 3, i32 0, i8 0, i32 undef)  ; LoadInput(inputSigId,rowIndex,colIndex,gsVertexAxis)
  %198 = mul i32 %197, 44
  %199 = add i32 %198, 18
  %200 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %3, %dx.types.ResourceProperties { i32 12, i32 16 })  ; AnnotateHandle(res,props)  resource: StructuredBuffer<stride=16>
  %201 = call %dx.types.ResRet.f32 @dx.op.rawBufferLoad.f32(i32 139, %dx.types.Handle %200, i32 %199, i32 0, i8 15, i32 4)  ; RawBufferLoad(srv,index,elementOffset,mask,alignment)
  %202 = extractvalue %dx.types.ResRet.f32 %201, 0
  %203 = extractvalue %dx.types.ResRet.f32 %201, 1
  %204 = extractvalue %dx.types.ResRet.f32 %201, 2
  %205 = add i32 %198, 19
  %206 = call %dx.types.ResRet.f32 @dx.op.rawBufferLoad.f32(i32 139, %dx.types.Handle %200, i32 %205, i32 0, i8 15, i32 4)  ; RawBufferLoad(srv,index,elementOffset,mask,alignment)
  %207 = extractvalue %dx.types.ResRet.f32 %206, 0
  %208 = extractvalue %dx.types.ResRet.f32 %206, 1
  %209 = extractvalue %dx.types.ResRet.f32 %206, 2
  %210 = add i32 %198, 17
  %211 = call %dx.types.ResRet.f32 @dx.op.rawBufferLoad.f32(i32 139, %dx.types.Handle %200, i32 %210, i32 0, i8 15, i32 4)  ; RawBufferLoad(srv,index,elementOffset,mask,alignment)
  %212 = extractvalue %dx.types.ResRet.f32 %211, 3
  %213 = fmul float %202, 2.097152e+06
  %214 = fmul float %203, 2.097152e+06
  %215 = fmul float %204, 2.097152e+06
  %216 = fadd float %213, %207
  %217 = fadd float %214, %208
  %218 = fadd float %215, %209
  %219 = fsub float %216, %213
  %220 = fsub float %217, %214
  %221 = fsub float %218, %215
  %222 = fsub float %207, %219
  %223 = fsub float %208, %220
  %224 = fsub float %209, %221
  %225 = add i32 %198, 26
  %226 = call %dx.types.ResRet.f32 @dx.op.rawBufferLoad.f32(i32 139, %dx.types.Handle %200, i32 %225, i32 0, i8 15, i32 4)  ; RawBufferLoad(srv,index,elementOffset,mask,alignment)
  %227 = extractvalue %dx.types.ResRet.f32 %226, 3
  %228 = add i32 %198, 27
  %229 = call %dx.types.ResRet.f32 @dx.op.rawBufferLoad.f32(i32 139, %dx.types.Handle %200, i32 %228, i32 0, i8 15, i32 4)  ; RawBufferLoad(srv,index,elementOffset,mask,alignment)
  %230 = extractvalue %dx.types.ResRet.f32 %229, 3
  %231 = add i32 %198, 32
  %232 = call %dx.types.ResRet.f32 @dx.op.rawBufferLoad.f32(i32 139, %dx.types.Handle %200, i32 %231, i32 0, i8 15, i32 4)  ; RawBufferLoad(srv,index,elementOffset,mask,alignment)
  %233 = extractvalue %dx.types.ResRet.f32 %232, 0
  %234 = fsub float %71, %216
  %235 = fsub float %72, %217
  %236 = fsub float %73, %218
  %237 = fsub float %77, %222
  %238 = fsub float %78, %223
  %239 = fsub float %79, %224
  %240 = fadd float %234, %237
  %241 = fadd float %235, %238
  %242 = fadd float %236, %239
  %243 = call float @dx.op.unary.f32(i32 6, float %240)  ; FAbs(value)
  %244 = call float @dx.op.unary.f32(i32 6, float %241)  ; FAbs(value)
  %245 = call float @dx.op.unary.f32(i32 6, float %242)  ; FAbs(value)
  %246 = fadd fast float %212, 1.000000e+00
  %247 = fadd fast float %227, 1.000000e+00
  %248 = fadd fast float %230, 1.000000e+00
  %249 = fcmp fast ogt float %243, %246
  %250 = fcmp fast ogt float %244, %247
  %251 = fcmp fast ogt float %245, %248
  %252 = or i1 %249, %250
  %253 = or i1 %252, %251
  br i1 %253, label %254, label %281

; <label>:254                                     ; preds = %196
  %255 = fmul fast float %77, 0x3EF0000000000000
  %256 = fmul fast float %78, 0x3EF0000000000000
  %257 = fmul fast float %79, 0x3EF0000000000000
  %258 = fmul fast float %71, 0x3EF0000000000000
  %259 = fmul fast float %72, 0x3EF0000000000000
  %260 = fmul fast float %73, 0x3EF0000000000000
  %261 = call float @dx.op.unary.f32(i32 22, float %258)  ; Frc(value)
  %262 = call float @dx.op.unary.f32(i32 22, float %259)  ; Frc(value)
  %263 = call float @dx.op.unary.f32(i32 22, float %260)  ; Frc(value)
  %264 = call float @dx.op.unary.f32(i32 22, float %255)  ; Frc(value)
  %265 = call float @dx.op.unary.f32(i32 22, float %256)  ; Frc(value)
  %266 = call float @dx.op.unary.f32(i32 22, float %257)  ; Frc(value)
  %267 = fadd fast float %264, %261
  %268 = fadd fast float %265, %262
  %269 = fadd fast float %266, %263
  %270 = call float @dx.op.unary.f32(i32 22, float %267)  ; Frc(value)
  %271 = call float @dx.op.unary.f32(i32 22, float %268)  ; Frc(value)
  %272 = call float @dx.op.unary.f32(i32 22, float %269)  ; Frc(value)
  %273 = fmul fast float %270, 6.553600e+04
  %274 = fmul fast float %271, 6.553600e+04
  %275 = fmul fast float %272, 6.553600e+04
  %276 = call float @dx.op.dot3.f32(i32 55, float %273, float %274, float %275, float 0x3F52E83A20000000, float 0x3F52E83A20000000, float 0x3F52E83A20000000)  ; Dot3(ax,ay,az,bx,by,bz)
  %277 = call float @dx.op.unary.f32(i32 22, float %276)  ; Frc(value)
  %278 = fcmp fast ogt float %277, 5.000000e-01
  %279 = uitofp i1 %278 to float
  %280 = fsub fast float 1.000000e+00, %279
  br label %297

; <label>:281                                     ; preds = %196
  %282 = fcmp fast ogt float %233, 0.000000e+00
  br i1 %282, label %283, label %297

; <label>:283                                     ; preds = %281
  %284 = fsub fast float %65, %13
  %285 = fsub fast float %66, %14
  %286 = fsub fast float %67, %15
  %287 = call float @dx.op.unary.f32(i32 6, float %284)  ; FAbs(value)
  %288 = call float @dx.op.unary.f32(i32 6, float %285)  ; FAbs(value)
  %289 = call float @dx.op.unary.f32(i32 6, float %286)  ; FAbs(value)
  %290 = call float @dx.op.binary.f32(i32 35, float %288, float %289)  ; FMax(a,b)
  %291 = call float @dx.op.binary.f32(i32 35, float %287, float %290)  ; FMax(a,b)
  %292 = fsub fast float %291, %233
  %293 = call float @dx.op.unary.f32(i32 6, float %292)  ; FAbs(value)
  %294 = fmul fast float %293, 2.000000e+01
  %295 = call float @dx.op.unary.f32(i32 7, float %294)  ; Saturate(value)
  %296 = fsub fast float 1.000000e+00, %295
  br label %297

; <label>:297                                     ; preds = %283, %281, %254, %0
  %298 = phi float [ %280, %254 ], [ %296, %283 ], [ %190, %281 ], [ %190, %0 ]
  %299 = phi float [ 1.000000e+00, %254 ], [ 0.000000e+00, %283 ], [ %191, %281 ], [ %191, %0 ]
  %300 = phi float [ %279, %254 ], [ %296, %283 ], [ %192, %281 ], [ %192, %0 ]
  %301 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %9, i32 156)  ; CBufferLoadLegacy(handle,regIndex)
  %302 = extractvalue %dx.types.CBufRet.f32 %301, 2
  %303 = fmul fast float %302, %298
  %304 = fmul fast float %302, %299
  %305 = fmul fast float %302, %300
  %306 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %9, i32 320)  ; CBufferLoadLegacy(handle,regIndex)
  %307 = extractvalue %dx.types.CBufRet.f32 %306, 2
  %308 = call float @dx.op.binary.f32(i32 36, float %303, float %307)  ; FMin(a,b)
  %309 = call float @dx.op.binary.f32(i32 36, float %304, float %307)  ; FMin(a,b)
  %310 = call float @dx.op.binary.f32(i32 36, float %305, float %307)  ; FMin(a,b)
  call void @dx.op.storeOutput.f32(i32 5, i32 0, i32 0, i8 0, float %308)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 0, i32 0, i8 1, float %309)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 0, i32 0, i8 2, float %310)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 0, i32 0, i8 3, float 0.000000e+00)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 1, i32 0, i8 0, float 0.000000e+00)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 1, i32 0, i8 1, float 0.000000e+00)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 1, i32 0, i8 2, float 0.000000e+00)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 1, i32 0, i8 3, float 0.000000e+00)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 2, i32 0, i8 0, float 0.000000e+00)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 2, i32 0, i8 1, float 0.000000e+00)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 2, i32 0, i8 2, float 0.000000e+00)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 2, i32 0, i8 3, float 0.000000e+00)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 3, i32 0, i8 0, float 0.000000e+00)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 3, i32 0, i8 1, float 0.000000e+00)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 3, i32 0, i8 2, float 0.000000e+00)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 3, i32 0, i8 3, float 0.000000e+00)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
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
declare %dx.types.ResRet.f32 @dx.op.sample.f32(i32, %dx.types.Handle, %dx.types.Handle, float, float, float, float, i32, i32, i32, float) #2

; Function Attrs: nounwind readonly
declare %dx.types.ResRet.f32 @dx.op.rawBufferLoad.f32(i32, %dx.types.Handle, i32, i32, i8, i32) #2

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
!dx.viewIdState = !{!17}
!dx.entryPoints = !{!18}

!0 = !{!"dxc(private) 1.8.0.0 (private, 00000000)"}
!1 = !{i32 1, i32 6}
!2 = !{i32 1, i32 8}
!3 = !{!"ps", i32 6, i32 6}
!4 = !{!5, null, !11, !14}
!5 = !{!6, !8, !10}
!6 = !{i32 0, %"class.StructuredBuffer<vector<float, 4> >"* undef, !"", i32 0, i32 0, i32 1, i32 12, i32 0, !7}
!7 = !{i32 1, i32 16}
!8 = !{i32 1, %"class.Texture2D<vector<float, 4> >"* undef, !"", i32 0, i32 1, i32 1, i32 2, i32 0, !9}
!9 = !{i32 0, i32 9}
!10 = !{i32 2, %"class.Texture2D<vector<float, 4> >"* undef, !"", i32 0, i32 2, i32 1, i32 2, i32 0, !9}
!11 = !{!12, !13}
!12 = !{i32 0, %hostlayout.View* undef, !"", i32 0, i32 0, i32 1, i32 10076, null}
!13 = !{i32 1, %Material* undef, !"", i32 0, i32 1, i32 1, i32 76, null}
!14 = !{!15, !16}
!15 = !{i32 0, %struct.SamplerState* undef, !"", i32 0, i32 0, i32 1, i32 0, null}
!16 = !{i32 1, %struct.SamplerState* undef, !"", i32 0, i32 1, i32 1, i32 0, null}
!17 = !{[26 x i32] [i32 24, i32 16, i32 0, i32 0, i32 0, i32 0, i32 0, i32 0, i32 0, i32 0, i32 7, i32 7, i32 0, i32 0, i32 7, i32 0, i32 0, i32 0, i32 5, i32 5, i32 5, i32 0, i32 7, i32 7, i32 7, i32 0]}
!18 = !{void ()* @MainPS, !"MainPS", !19, !4, !42}
!19 = !{!20, !33, null}
!20 = !{!21, !23, !24, !26, !28, !31, !32}
!21 = !{i32 0, !"TEXCOORD10_centroid", i8 9, i8 0, !22, i8 2, i32 1, i8 4, i32 0, i8 0, null}
!22 = !{i32 0}
!23 = !{i32 1, !"TEXCOORD11_centroid", i8 9, i8 0, !22, i8 2, i32 1, i8 4, i32 1, i8 0, null}
!24 = !{i32 2, !"TEXCOORD", i8 9, i8 0, !22, i8 2, i32 1, i8 4, i32 2, i8 0, !25}
!25 = !{i32 3, i32 3}
!26 = !{i32 3, !"PRIMITIVE_ID", i8 5, i8 0, !22, i8 1, i32 1, i8 1, i32 3, i8 0, !27}
!27 = !{i32 3, i32 1}
!28 = !{i32 4, !"TEXCOORD", i8 9, i8 0, !29, i8 2, i32 1, i8 3, i32 4, i8 0, !30}
!29 = !{i32 9}
!30 = !{i32 3, i32 7}
!31 = !{i32 5, !"SV_Position", i8 9, i8 3, !22, i8 4, i32 1, i8 4, i32 5, i8 0, !30}
!32 = !{i32 6, !"SV_IsFrontFace", i8 5, i8 13, !22, i8 1, i32 1, i8 1, i32 3, i8 1, null}
!33 = !{!34, !36, !38, !40}
!34 = !{i32 0, !"SV_Target", i8 9, i8 16, !22, i8 0, i32 1, i8 4, i32 0, i8 0, !35}
!35 = !{i32 3, i32 15}
!36 = !{i32 1, !"SV_Target", i8 9, i8 16, !37, i8 0, i32 1, i8 4, i32 1, i8 0, !35}
!37 = !{i32 1}
!38 = !{i32 2, !"SV_Target", i8 9, i8 16, !39, i8 0, i32 1, i8 4, i32 2, i8 0, !35}
!39 = !{i32 2}
!40 = !{i32 3, !"SV_Target", i8 9, i8 16, !41, i8 0, i32 1, i8 4, i32 3, i8 0, !35}
!41 = !{i32 3}
!42 = !{i32 0, i64 16, i32 5, !22}
!43 = distinct !{!43, !"dx.controlflow.hints", i32 1}
