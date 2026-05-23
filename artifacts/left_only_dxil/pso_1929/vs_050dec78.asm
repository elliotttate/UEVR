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
; TEXCOORD                 0   xyzw        2     NONE   float   xyzw
; PRIMITIVE_ID             0   x           3     NONE    uint   x   
; TEXCOORD                 9   xyz         4     NONE   float   xyz 
; SV_Position              0   xyzw        5      POS   float   xyzw
;
; shader debug name: 93c24ef24387646036c6c14ade2f32ff.pdb
; shader hash: 93c24ef24387646036c6c14ade2f32ff
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
; TEXCOORD                 0                 linear       
; PRIMITIVE_ID             0        nointerpolation       
; TEXCOORD                 9                 linear       
; SV_Position              0          noperspective       
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
;                                   texture  struct         r/o      T0             t0     1
;                                   texture  struct         r/o      T1             t1     1
;                                   texture  struct         r/o      T2             t2     1
;                                   texture     f32         buf      T3             t3     1
;                                   texture     f32         buf      T4             t4     1
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
;   output 8 depends on inputs: { 12 }
;   output 9 depends on inputs: { 12 }
;   output 12 depends on inputs: { 4, 8 }
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
%dx.types.ResRet.f32 = type { float, float, float, float, i32 }
%dx.types.ResRet.i32 = type { i32, i32, i32, i32, i32 }
%dx.types.CBufRet.i32 = type { i32, i32, i32, i32 }
%"class.StructuredBuffer<unsigned int>" = type { i32 }
%"class.StructuredBuffer<vector<float, 4> >" = type { <4 x float> }
%"class.Buffer<vector<float, 2> >" = type { <2 x float> }
%"class.Buffer<vector<float, 4> >" = type { <4 x float> }
%hostlayout.View = type { [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <4 x float>, <4 x float>, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], <4 x float>, <4 x float>, <2 x float>, <2 x float>, <2 x float>, <2 x float>, <4 x float>, <4 x float>, <4 x i32>, <4 x float>, <4 x float>, <4 x float>, <4 x float>, <2 x float>, <2 x float>, i32, float, float, float, <4 x float>, <4 x float>, <4 x float>, <2 x float>, <2 x float>, float, float, float, float, <3 x float>, float, float, float, float, float, float, float, float, i32, i32, i32, i32, i32, i32, i32, i32, float, float, float, float, float, <4 x float>, <3 x float>, float, [2 x <4 x float>], [2 x <4 x float>], <4 x float>, <4 x float>, float, float, float, float, float, float, float, float, float, float, float, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, [2 x <4 x float>], [2 x <4 x float>], [2 x <4 x float>], [2 x <4 x float>], [2 x <4 x float>], <4 x float>, <3 x float>, float, <4 x float>, [4 x <4 x float>], <4 x float>, float, float, float, float, <4 x float>, float, float, float, float, float, float, float, float, <3 x float>, float, float, float, float, float, <4 x float>, float, float, float, float, <4 x float>, float, float, float, float, [8 x <4 x float>], float, float, float, float, i32, float, float, float, <3 x float>, i32, [6 x <4 x float>], [6 x <4 x float>], [6 x <4 x float>], [6 x <4 x float>], float, float, i32, i32, <3 x float>, float, <3 x float>, float, float, float, i32, float, float, float, float, float, float, float, <2 x i32>, float, float, float, float, <3 x float>, float, <3 x float>, float, <2 x float>, <2 x float>, <2 x float>, <2 x float>, <2 x float>, float, float, <3 x float>, float, <2 x float>, <2 x float>, float, float, float, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, float, float, float, float, [2 x <4 x float>], <4 x float>, i32, i32, i32, i32, i32, i32, i32, float, <4 x float>, i32, i32, i32, i32, <4 x i32>, <2 x float>, float, float, <4 x float>, <4 x float>, <4 x float>, <3 x float>, float, i32, i32, i32, i32, [32 x <4 x i32>], i32, float, float, float, <4 x float>, <4 x float>, <4 x float>, <4 x float>, <2 x float>, float, float, <4 x float>, <4 x float>, <4 x float>, float, float, float, i32, <4 x i32>, i32, i32, i32, i32, [4 x <4 x float>], <3 x i32>, i32, <3 x float>, float, <3 x float>, float, <3 x i32>, i32, [16 x <4 x float>], float, float, float, float, float, float, float, float, [4 x [4 x <4 x float>]], [4 x <4 x float>], [4 x <4 x float>], [16 x [4 x <4 x float>]], [16 x <4 x float>], [16 x <4 x float>], [8 x [4 x <4 x float>]], [8 x <4 x float>], [8 x <4 x float>], [8 x <4 x float>], [8 x <4 x float>], [2 x <4 x float>], [2 x <4 x float>], [2 x <4 x float>], [2 x <4 x float>], [2 x <4 x float>], [2 x <4 x float>], [2 x <4 x float>], [2 x <4 x float>], [2 x <4 x float>], [2 x <4 x float>], <4 x float>, <4 x float>, float, i32, i32, float, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, <3 x float>, i32, <4 x float>, <4 x float>, <4 x float>, <3 x float>, i32, <3 x i32>, i32, <3 x i32>, i32, <3 x i32>, i32, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, <4 x float>, float, float, i32, i32, i32, i32, i32, i32, <4 x float>, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, <3 x i32>, i32, <3 x i32>, i32, <3 x float>, float, <3 x float> }
%Scene = type { i32, i32, i32, i32, i32, i32, i32, float, i32, i32, i32, i32, i32, i32, i32, i32, i32, float, float, float, i32, i32, i32, i32, i32, i32, i32, i32, <4 x i32>, i32, i32, i32, i32, <4 x i32>, i32, i32, i32, i32, i32, float, float, float, i32, i32, i32, i32, i32, i32, i32, float, i32, i32, i32, i32, i32, i32, i32, float, <2 x float>, i32, i32, i32, i32, i32, float, i32, i32, i32, i32, i32, float, float, float, i32, i32, i32, float, i32, i32, i32 }
%LocalVF = type { <4 x i32>, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32 }

define void @Main() {
  %1 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 4, i32 4, i32 0, i8 0 }, i32 4, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %2 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 3, i32 3, i32 0, i8 0 }, i32 3, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %3 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 2, i32 2, i32 0, i8 0 }, i32 2, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %4 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 1, i32 1, i32 0, i8 0 }, i32 1, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %5 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind zeroinitializer, i32 0, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %6 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 2, i32 2, i32 0, i8 2 }, i32 2, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %7 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 1, i32 1, i32 0, i8 2 }, i32 1, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %8 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 0, i32 0, i32 0, i8 2 }, i32 0, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %9 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %6, %dx.types.ResourceProperties { i32 13, i32 60 })  ; AnnotateHandle(res,props)  resource: CBuffer
  %10 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %7, %dx.types.ResourceProperties { i32 13, i32 348 })  ; AnnotateHandle(res,props)  resource: CBuffer
  %11 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %8, %dx.types.ResourceProperties { i32 13, i32 10076 })  ; AnnotateHandle(res,props)  resource: CBuffer
  %12 = call i32 @dx.op.loadInput.i32(i32 4, i32 3, i32 0, i8 0, i32 undef)  ; LoadInput(inputSigId,rowIndex,colIndex,gsVertexAxis)
  %13 = call i32 @dx.op.loadInput.i32(i32 4, i32 2, i32 0, i8 0, i32 undef)  ; LoadInput(inputSigId,rowIndex,colIndex,gsVertexAxis)
  %14 = call i32 @dx.op.loadInput.i32(i32 4, i32 1, i32 0, i8 0, i32 undef)  ; LoadInput(inputSigId,rowIndex,colIndex,gsVertexAxis)
  %15 = call float @dx.op.loadInput.f32(i32 4, i32 0, i32 0, i8 0, i32 undef), !dx.precise !40  ; LoadInput(inputSigId,rowIndex,colIndex,gsVertexAxis)
  %16 = call float @dx.op.loadInput.f32(i32 4, i32 0, i32 0, i8 1, i32 undef), !dx.precise !40  ; LoadInput(inputSigId,rowIndex,colIndex,gsVertexAxis)
  %17 = call float @dx.op.loadInput.f32(i32 4, i32 0, i32 0, i8 2, i32 undef), !dx.precise !40  ; LoadInput(inputSigId,rowIndex,colIndex,gsVertexAxis)
  %18 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %11, i32 0)  ; CBufferLoadLegacy(handle,regIndex)
  %19 = extractvalue %dx.types.CBufRet.f32 %18, 0
  %20 = extractvalue %dx.types.CBufRet.f32 %18, 1
  %21 = extractvalue %dx.types.CBufRet.f32 %18, 2
  %22 = extractvalue %dx.types.CBufRet.f32 %18, 3
  %23 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %11, i32 1)  ; CBufferLoadLegacy(handle,regIndex)
  %24 = extractvalue %dx.types.CBufRet.f32 %23, 0
  %25 = extractvalue %dx.types.CBufRet.f32 %23, 1
  %26 = extractvalue %dx.types.CBufRet.f32 %23, 2
  %27 = extractvalue %dx.types.CBufRet.f32 %23, 3
  %28 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %11, i32 2)  ; CBufferLoadLegacy(handle,regIndex)
  %29 = extractvalue %dx.types.CBufRet.f32 %28, 0
  %30 = extractvalue %dx.types.CBufRet.f32 %28, 1
  %31 = extractvalue %dx.types.CBufRet.f32 %28, 2
  %32 = extractvalue %dx.types.CBufRet.f32 %28, 3
  %33 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %11, i32 3)  ; CBufferLoadLegacy(handle,regIndex)
  %34 = extractvalue %dx.types.CBufRet.f32 %33, 0
  %35 = extractvalue %dx.types.CBufRet.f32 %33, 1
  %36 = extractvalue %dx.types.CBufRet.f32 %33, 2
  %37 = extractvalue %dx.types.CBufRet.f32 %33, 3
  %38 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %11, i32 64)  ; CBufferLoadLegacy(handle,regIndex)
  %39 = extractvalue %dx.types.CBufRet.f32 %38, 0
  %40 = extractvalue %dx.types.CBufRet.f32 %38, 1
  %41 = extractvalue %dx.types.CBufRet.f32 %38, 2
  %42 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %11, i32 65)  ; CBufferLoadLegacy(handle,regIndex)
  %43 = extractvalue %dx.types.CBufRet.f32 %42, 0
  %44 = extractvalue %dx.types.CBufRet.f32 %42, 1
  %45 = extractvalue %dx.types.CBufRet.f32 %42, 2
  %46 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %11, i32 66)  ; CBufferLoadLegacy(handle,regIndex)
  %47 = extractvalue %dx.types.CBufRet.f32 %46, 0
  %48 = extractvalue %dx.types.CBufRet.f32 %46, 1
  %49 = extractvalue %dx.types.CBufRet.f32 %46, 2
  %50 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %11, i32 84)  ; CBufferLoadLegacy(handle,regIndex)
  %51 = extractvalue %dx.types.CBufRet.f32 %50, 0
  %52 = extractvalue %dx.types.CBufRet.f32 %50, 1
  %53 = extractvalue %dx.types.CBufRet.f32 %50, 2
  %54 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %11, i32 85)  ; CBufferLoadLegacy(handle,regIndex)
  %55 = extractvalue %dx.types.CBufRet.f32 %54, 0
  %56 = extractvalue %dx.types.CBufRet.f32 %54, 1
  %57 = extractvalue %dx.types.CBufRet.f32 %54, 2
  %58 = icmp slt i32 %14, 0
  br i1 %58, label %59, label %67

; <label>:59                                      ; preds = %0
  %60 = and i32 %14, 2147483647
  %61 = mul i32 %60, 44
  %62 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %3, %dx.types.ResourceProperties { i32 12, i32 16 })  ; AnnotateHandle(res,props)  resource: StructuredBuffer<stride=16>
  %63 = call %dx.types.ResRet.f32 @dx.op.rawBufferLoad.f32(i32 139, %dx.types.Handle %62, i32 %61, i32 0, i8 15, i32 4)  ; RawBufferLoad(srv,index,elementOffset,mask,alignment)
  %64 = extractvalue %dx.types.ResRet.f32 %63, 1
  %65 = bitcast float %64 to i32
  %66 = add i32 %65, %13
  br label %73

; <label>:67                                      ; preds = %0
  %68 = add i32 %14, %13
  %69 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %5, %dx.types.ResourceProperties { i32 12, i32 4 })  ; AnnotateHandle(res,props)  resource: StructuredBuffer<stride=4>
  %70 = call %dx.types.ResRet.i32 @dx.op.rawBufferLoad.i32(i32 139, %dx.types.Handle %69, i32 %68, i32 0, i8 1, i32 4)  ; RawBufferLoad(srv,index,elementOffset,mask,alignment)
  %71 = extractvalue %dx.types.ResRet.i32 %70, 0
  %72 = and i32 %71, 16777215
  br label %73

; <label>:73                                      ; preds = %67, %59
  %74 = phi i32 [ %66, %59 ], [ %72, %67 ]
  %75 = call %dx.types.CBufRet.i32 @dx.op.cbufferLoadLegacy.i32(i32 59, %dx.types.Handle %10, i32 0)  ; CBufferLoadLegacy(handle,regIndex)
  %76 = extractvalue %dx.types.CBufRet.i32 %75, 0
  %77 = extractvalue %dx.types.CBufRet.i32 %75, 1
  %78 = extractvalue %dx.types.CBufRet.i32 %75, 2
  %79 = and i32 %76, 31
  %80 = lshr i32 %74, %79
  %81 = and i32 %77, %74
  %82 = mul i32 %78, %80
  %83 = add i32 %82, %81
  %84 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %4, %dx.types.ResourceProperties { i32 12, i32 16 })  ; AnnotateHandle(res,props)  resource: StructuredBuffer<stride=16>
  %85 = call %dx.types.ResRet.f32 @dx.op.rawBufferLoad.f32(i32 139, %dx.types.Handle %84, i32 %83, i32 0, i8 15, i32 4)  ; RawBufferLoad(srv,index,elementOffset,mask,alignment)
  %86 = extractvalue %dx.types.ResRet.f32 %85, 0
  %87 = bitcast float %86 to i32
  %88 = and i32 %87, 1048575
  %89 = lshr i32 %87, 20
  %90 = icmp ne i32 %88, 1048575
  %91 = and i32 %89, 1024
  %92 = icmp eq i32 %91, 0
  %93 = and i1 %90, %92
  %94 = mul nuw nsw i32 %88, 44
  br i1 %93, label %95, label %215, !dx.controlflow.hints !41

; <label>:95                                      ; preds = %73
  %96 = or i32 %94, 1
  %97 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %3, %dx.types.ResourceProperties { i32 12, i32 16 })  ; AnnotateHandle(res,props)  resource: StructuredBuffer<stride=16>
  %98 = call %dx.types.ResRet.f32 @dx.op.rawBufferLoad.f32(i32 139, %dx.types.Handle %97, i32 %96, i32 0, i8 15, i32 4)  ; RawBufferLoad(srv,index,elementOffset,mask,alignment)
  %99 = extractvalue %dx.types.ResRet.f32 %98, 0
  %100 = extractvalue %dx.types.ResRet.f32 %98, 1
  %101 = extractvalue %dx.types.ResRet.f32 %98, 2
  %102 = shl i32 1, %79
  %103 = add i32 %81, %102
  %104 = add i32 %103, %82
  %105 = call %dx.types.ResRet.f32 @dx.op.rawBufferLoad.f32(i32 139, %dx.types.Handle %84, i32 %104, i32 0, i8 15, i32 4)  ; RawBufferLoad(srv,index,elementOffset,mask,alignment)
  %106 = extractvalue %dx.types.ResRet.f32 %105, 0
  %107 = extractvalue %dx.types.ResRet.f32 %105, 1
  %108 = extractvalue %dx.types.ResRet.f32 %105, 2
  %109 = extractvalue %dx.types.ResRet.f32 %105, 3
  %110 = bitcast float %106 to i32
  %111 = bitcast float %107 to i32
  %112 = bitcast float %108 to i32
  %113 = bitcast float %109 to i32
  %114 = shl i32 2, %79
  %115 = add i32 %81, %114
  %116 = add i32 %115, %82
  %117 = call %dx.types.ResRet.f32 @dx.op.rawBufferLoad.f32(i32 139, %dx.types.Handle %84, i32 %116, i32 0, i8 15, i32 4)  ; RawBufferLoad(srv,index,elementOffset,mask,alignment)
  %118 = extractvalue %dx.types.ResRet.f32 %117, 0
  %119 = extractvalue %dx.types.ResRet.f32 %117, 1
  %120 = extractvalue %dx.types.ResRet.f32 %117, 2
  %121 = and i32 %110, 65535
  %122 = uitofp i32 %121 to float
  %123 = lshr i32 %110, 16
  %124 = uitofp i32 %123 to float
  %125 = and i32 %111, 32767
  %126 = uitofp i32 %125 to float
  %127 = fadd float %122, -3.276800e+04
  %128 = fadd float %124, -3.276800e+04
  %129 = fmul float %127, 0x3F00002000000000
  %130 = fmul float %128, 0x3F00002000000000
  %131 = fadd float %126, -1.638400e+04
  %132 = fmul float %131, 0x3F06A0F8E0000000
  %133 = and i32 %111, 32768
  %134 = icmp ne i32 %133, 0
  %135 = fadd float %129, %130
  %136 = fsub float %129, %130
  %137 = call float @dx.op.unary.f32(i32 6, float %135), !dx.precise !40  ; FAbs(value)
  %138 = call float @dx.op.unary.f32(i32 6, float %136), !dx.precise !40  ; FAbs(value)
  %139 = call float @dx.op.dot2.f32(i32 54, float 1.000000e+00, float 1.000000e+00, float %137, float %138), !dx.precise !40  ; Dot2(ax,ay,bx,by)
  %140 = fsub float 2.000000e+00, %139
  %141 = call float @dx.op.dot3.f32(i32 55, float %135, float %136, float %140, float %135, float %136, float %140), !dx.precise !40  ; Dot3(ax,ay,az,bx,by,bz)
  %142 = call float @dx.op.unary.f32(i32 25, float %141), !dx.precise !40  ; Rsqrt(value)
  %143 = fmul float %135, %142
  %144 = fmul float %136, %142
  %145 = fmul float %140, %142
  %146 = fadd float %145, 1.000000e+00
  %147 = fdiv float 1.000000e+00, %146
  %148 = fmul float %143, %144
  %149 = fmul float %147, %148
  %150 = fsub float -0.000000e+00, %149
  %151 = fmul float %143, %143
  %152 = fmul float %147, %151
  %153 = fsub float 1.000000e+00, %152
  %154 = fmul float %144, %144
  %155 = fmul float %147, %154
  %156 = fsub float 1.000000e+00, %155
  %157 = fmul float %132, %132
  %158 = fsub float 1.000000e+00, %157
  %159 = call float @dx.op.unary.f32(i32 24, float %158), !dx.precise !40  ; Sqrt(value)
  %160 = select i1 %134, float %132, float %159
  %161 = select i1 %134, float %159, float %132
  %162 = fmul float %160, %153
  %163 = fmul float %160, %150
  %164 = fmul float %143, %160
  %165 = fmul float %161, %150
  %166 = fmul float %161, %156
  %167 = fmul float %144, %161
  %168 = fsub float -0.000000e+00, %167
  %169 = fadd float %165, %162
  %170 = fadd float %163, %166
  %171 = fsub float %168, %164
  %172 = fmul float %144, %171
  %173 = fmul float %145, %170
  %174 = fsub float %172, %173
  %175 = fmul float %145, %169
  %176 = fmul float %143, %171
  %177 = fsub float %175, %176
  %178 = fmul float %143, %170
  %179 = fmul float %144, %169
  %180 = fsub float %178, %179
  %181 = lshr i32 %113, 16
  %182 = shl i32 %181, 23
  %183 = add i32 %182, -125829120
  %184 = bitcast i32 %183 to float
  %185 = lshr i32 %112, 16
  %186 = and i32 %112, 65535
  %187 = and i32 %113, 65535
  %188 = uitofp i32 %186 to float
  %189 = uitofp i32 %185 to float
  %190 = uitofp i32 %187 to float
  %191 = fadd float %188, -3.276800e+04
  %192 = fadd float %189, -3.276800e+04
  %193 = fadd float %190, -3.276800e+04
  %194 = fmul float %191, %184
  %195 = fmul float %192, %184
  %196 = fmul float %193, %184
  %197 = fmul float %194, %169
  %198 = fmul float %194, %170
  %199 = fmul float %194, %171
  %200 = fmul float %195, %174
  %201 = fmul float %195, %177
  %202 = fmul float %195, %180
  %203 = fmul float %196, %143
  %204 = fmul float %196, %144
  %205 = fmul float %196, %145
  %206 = call float @dx.op.unary.f32(i32 6, float %194)  ; FAbs(value)
  %207 = call float @dx.op.unary.f32(i32 6, float %195)  ; FAbs(value)
  %208 = call float @dx.op.unary.f32(i32 6, float %196)  ; FAbs(value)
  %209 = fdiv fast float 1.000000e+00, %206
  %210 = fdiv fast float 1.000000e+00, %207
  %211 = fdiv fast float 1.000000e+00, %208
  %212 = and i32 %89, 1
  %213 = icmp ne i32 %212, 0
  %214 = select i1 %213, float -1.000000e+00, float 1.000000e+00
  br label %215

; <label>:215                                     ; preds = %95, %73
  %216 = phi float [ %209, %95 ], [ 0.000000e+00, %73 ]
  %217 = phi float [ %210, %95 ], [ 0.000000e+00, %73 ]
  %218 = phi float [ %211, %95 ], [ 0.000000e+00, %73 ]
  %219 = phi float [ %214, %95 ], [ 0.000000e+00, %73 ]
  %220 = phi float [ %99, %95 ], [ 0.000000e+00, %73 ]
  %221 = phi float [ %100, %95 ], [ 0.000000e+00, %73 ]
  %222 = phi float [ %101, %95 ], [ 0.000000e+00, %73 ]
  %223 = phi float [ %197, %95 ], [ 0.000000e+00, %73 ]
  %224 = phi float [ %198, %95 ], [ 0.000000e+00, %73 ]
  %225 = phi float [ %199, %95 ], [ 0.000000e+00, %73 ]
  %226 = phi float [ %200, %95 ], [ 0.000000e+00, %73 ]
  %227 = phi float [ %201, %95 ], [ 0.000000e+00, %73 ]
  %228 = phi float [ %202, %95 ], [ 0.000000e+00, %73 ]
  %229 = phi float [ %203, %95 ], [ 0.000000e+00, %73 ]
  %230 = phi float [ %204, %95 ], [ 0.000000e+00, %73 ]
  %231 = phi float [ %205, %95 ], [ 0.000000e+00, %73 ]
  %232 = phi float [ %118, %95 ], [ 0.000000e+00, %73 ]
  %233 = phi float [ %119, %95 ], [ 0.000000e+00, %73 ]
  %234 = phi float [ %120, %95 ], [ 0.000000e+00, %73 ]
  %235 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %3, %dx.types.ResourceProperties { i32 12, i32 16 })  ; AnnotateHandle(res,props)  resource: StructuredBuffer<stride=16>
  %236 = call %dx.types.ResRet.f32 @dx.op.rawBufferLoad.f32(i32 139, %dx.types.Handle %235, i32 %94, i32 0, i8 15, i32 4)  ; RawBufferLoad(srv,index,elementOffset,mask,alignment)
  %237 = extractvalue %dx.types.ResRet.f32 %236, 0
  %238 = bitcast float %237 to i32
  %239 = call %dx.types.CBufRet.i32 @dx.op.cbufferLoadLegacy.i32(i32 59, %dx.types.Handle %9, i32 0)  ; CBufferLoadLegacy(handle,regIndex)
  %240 = extractvalue %dx.types.CBufRet.i32 %239, 3
  %241 = add i32 %240, %12
  %242 = shl i32 %241, 1
  %243 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %1, %dx.types.ResourceProperties { i32 10, i32 1033 })  ; AnnotateHandle(res,props)  resource: TypedBuffer<4xF32>
  %244 = call %dx.types.ResRet.f32 @dx.op.bufferLoad.f32(i32 68, %dx.types.Handle %243, i32 %242, i32 undef)  ; BufferLoad(srv,index,wot)
  %245 = extractvalue %dx.types.ResRet.f32 %244, 0
  %246 = extractvalue %dx.types.ResRet.f32 %244, 1
  %247 = extractvalue %dx.types.ResRet.f32 %244, 2
  %248 = or i32 %242, 1
  %249 = call %dx.types.ResRet.f32 @dx.op.bufferLoad.f32(i32 68, %dx.types.Handle %243, i32 %248, i32 undef)  ; BufferLoad(srv,index,wot)
  %250 = extractvalue %dx.types.ResRet.f32 %249, 0
  %251 = extractvalue %dx.types.ResRet.f32 %249, 1
  %252 = extractvalue %dx.types.ResRet.f32 %249, 2
  %253 = extractvalue %dx.types.ResRet.f32 %249, 3
  %254 = fmul fast float %251, %247
  %255 = fmul fast float %252, %246
  %256 = fsub fast float %254, %255
  %257 = fmul fast float %252, %245
  %258 = fmul fast float %250, %247
  %259 = fsub fast float %257, %258
  %260 = fmul fast float %250, %246
  %261 = fmul fast float %251, %245
  %262 = fsub fast float %260, %261
  %263 = fmul fast float %256, %253
  %264 = fmul fast float %259, %253
  %265 = fmul fast float %262, %253
  %266 = fmul fast float %264, %252
  %267 = fmul fast float %265, %251
  %268 = fsub fast float %266, %267
  %269 = fmul fast float %265, %250
  %270 = fmul fast float %263, %252
  %271 = fsub fast float %269, %270
  %272 = fmul fast float %263, %251
  %273 = fmul fast float %264, %250
  %274 = fsub fast float %272, %273
  %275 = fmul fast float %268, %253
  %276 = fmul fast float %271, %253
  %277 = fmul fast float %274, %253
  %278 = fmul fast float %223, %216
  %279 = fmul fast float %224, %216
  %280 = fmul fast float %225, %216
  %281 = fmul fast float %226, %217
  %282 = fmul fast float %227, %217
  %283 = fmul fast float %228, %217
  %284 = fmul fast float %229, %218
  %285 = fmul fast float %230, %218
  %286 = fmul fast float %231, %218
  %287 = fmul fast float %275, %278
  %288 = call float @dx.op.tertiary.f32(i32 46, float %276, float %281, float %287)  ; FMad(a,b,c)
  %289 = call float @dx.op.tertiary.f32(i32 46, float %277, float %284, float %288)  ; FMad(a,b,c)
  %290 = fmul fast float %275, %279
  %291 = call float @dx.op.tertiary.f32(i32 46, float %276, float %282, float %290)  ; FMad(a,b,c)
  %292 = call float @dx.op.tertiary.f32(i32 46, float %277, float %285, float %291)  ; FMad(a,b,c)
  %293 = fmul fast float %275, %280
  %294 = call float @dx.op.tertiary.f32(i32 46, float %276, float %283, float %293)  ; FMad(a,b,c)
  %295 = call float @dx.op.tertiary.f32(i32 46, float %277, float %286, float %294)  ; FMad(a,b,c)
  %296 = fmul fast float %250, %278
  %297 = call float @dx.op.tertiary.f32(i32 46, float %251, float %281, float %296)  ; FMad(a,b,c)
  %298 = call float @dx.op.tertiary.f32(i32 46, float %252, float %284, float %297)  ; FMad(a,b,c)
  %299 = fmul fast float %250, %279
  %300 = call float @dx.op.tertiary.f32(i32 46, float %251, float %282, float %299)  ; FMad(a,b,c)
  %301 = call float @dx.op.tertiary.f32(i32 46, float %252, float %285, float %300)  ; FMad(a,b,c)
  %302 = fmul fast float %250, %280
  %303 = call float @dx.op.tertiary.f32(i32 46, float %251, float %283, float %302)  ; FMad(a,b,c)
  %304 = call float @dx.op.tertiary.f32(i32 46, float %252, float %286, float %303)  ; FMad(a,b,c)
  %305 = fmul fast float %253, %219
  %306 = fmul float %17, %229
  %307 = fmul float %17, %230
  %308 = fmul float %17, %231
  %309 = call float @dx.op.tertiary.f32(i32 46, float %16, float %226, float %306), !dx.precise !40  ; FMad(a,b,c)
  %310 = call float @dx.op.tertiary.f32(i32 46, float %16, float %227, float %307), !dx.precise !40  ; FMad(a,b,c)
  %311 = call float @dx.op.tertiary.f32(i32 46, float %16, float %228, float %308), !dx.precise !40  ; FMad(a,b,c)
  %312 = call float @dx.op.tertiary.f32(i32 46, float %15, float %223, float %309), !dx.precise !40  ; FMad(a,b,c)
  %313 = call float @dx.op.tertiary.f32(i32 46, float %15, float %224, float %310), !dx.precise !40  ; FMad(a,b,c)
  %314 = call float @dx.op.tertiary.f32(i32 46, float %15, float %225, float %311), !dx.precise !40  ; FMad(a,b,c)
  %315 = fadd float %51, %220
  %316 = fadd float %52, %221
  %317 = fadd float %53, %222
  %318 = fadd float %55, %315
  %319 = fadd float %56, %316
  %320 = fadd float %57, %317
  %321 = fadd float %318, %232
  %322 = fadd float %319, %233
  %323 = fadd float %320, %234
  %324 = fadd float %321, %312
  %325 = fadd float %322, %313
  %326 = fadd float %323, %314
  %327 = extractvalue %dx.types.CBufRet.i32 %239, 1
  %328 = add i32 %327, -1
  %329 = call i32 @dx.op.binary.i32(i32 40, i32 0, i32 %328)  ; UMin(a,b)
  %330 = mul i32 %241, %327
  %331 = add i32 %330, %329
  %332 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %2, %dx.types.ResourceProperties { i32 10, i32 521 })  ; AnnotateHandle(res,props)  resource: TypedBuffer<2xF32>
  %333 = call %dx.types.ResRet.f32 @dx.op.bufferLoad.f32(i32 68, %dx.types.Handle %332, i32 %331, i32 undef)  ; BufferLoad(srv,index,wot)
  %334 = extractvalue %dx.types.ResRet.f32 %333, 0
  %335 = extractvalue %dx.types.ResRet.f32 %333, 1
  %336 = fadd float %324, 0.000000e+00
  %337 = fadd float %325, 0.000000e+00
  %338 = fadd float %326, 0.000000e+00
  %339 = and i32 %238, 1073741824
  %340 = icmp eq i32 %339, 0
  br i1 %340, label %357, label %341, !dx.controlflow.hints !42

; <label>:341                                     ; preds = %215
  %342 = fmul float %39, %336
  %343 = call float @dx.op.tertiary.f32(i32 46, float %337, float %43, float %342), !dx.precise !40  ; FMad(a,b,c)
  %344 = call float @dx.op.tertiary.f32(i32 46, float %338, float %47, float %343), !dx.precise !40  ; FMad(a,b,c)
  %345 = fmul float %40, %336
  %346 = call float @dx.op.tertiary.f32(i32 46, float %337, float %44, float %345), !dx.precise !40  ; FMad(a,b,c)
  %347 = call float @dx.op.tertiary.f32(i32 46, float %338, float %48, float %346), !dx.precise !40  ; FMad(a,b,c)
  %348 = fmul float %41, %336
  %349 = call float @dx.op.tertiary.f32(i32 46, float %337, float %45, float %348), !dx.precise !40  ; FMad(a,b,c)
  %350 = call float @dx.op.tertiary.f32(i32 46, float %338, float %49, float %349), !dx.precise !40  ; FMad(a,b,c)
  %351 = fsub float %344, %336
  %352 = fsub float %347, %337
  %353 = fsub float %350, %338
  %354 = fadd float %336, %351
  %355 = fadd float %337, %352
  %356 = fadd float %338, %353
  br label %357

; <label>:357                                     ; preds = %341, %215
  %358 = phi float [ %354, %341 ], [ %336, %215 ]
  %359 = phi float [ %355, %341 ], [ %337, %215 ]
  %360 = phi float [ %356, %341 ], [ %338, %215 ]
  %361 = fmul float %19, %358
  %362 = call float @dx.op.tertiary.f32(i32 46, float %359, float %24, float %361), !dx.precise !40  ; FMad(a,b,c)
  %363 = call float @dx.op.tertiary.f32(i32 46, float %360, float %29, float %362), !dx.precise !40  ; FMad(a,b,c)
  %364 = call float @dx.op.tertiary.f32(i32 46, float 1.000000e+00, float %34, float %363), !dx.precise !40  ; FMad(a,b,c)
  %365 = fmul float %20, %358
  %366 = call float @dx.op.tertiary.f32(i32 46, float %359, float %25, float %365), !dx.precise !40  ; FMad(a,b,c)
  %367 = call float @dx.op.tertiary.f32(i32 46, float %360, float %30, float %366), !dx.precise !40  ; FMad(a,b,c)
  %368 = call float @dx.op.tertiary.f32(i32 46, float 1.000000e+00, float %35, float %367), !dx.precise !40  ; FMad(a,b,c)
  %369 = fmul float %21, %358
  %370 = call float @dx.op.tertiary.f32(i32 46, float %359, float %26, float %369), !dx.precise !40  ; FMad(a,b,c)
  %371 = call float @dx.op.tertiary.f32(i32 46, float %360, float %31, float %370), !dx.precise !40  ; FMad(a,b,c)
  %372 = call float @dx.op.tertiary.f32(i32 46, float 1.000000e+00, float %36, float %371), !dx.precise !40  ; FMad(a,b,c)
  %373 = fmul float %22, %358
  %374 = call float @dx.op.tertiary.f32(i32 46, float %359, float %27, float %373), !dx.precise !40  ; FMad(a,b,c)
  %375 = call float @dx.op.tertiary.f32(i32 46, float %360, float %32, float %374), !dx.precise !40  ; FMad(a,b,c)
  %376 = call float @dx.op.tertiary.f32(i32 46, float 1.000000e+00, float %37, float %375), !dx.precise !40  ; FMad(a,b,c)
  call void @dx.op.storeOutput.f32(i32 5, i32 5, i32 0, i8 0, float %364)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 5, i32 0, i8 1, float %368)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 5, i32 0, i8 2, float %372)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 5, i32 0, i8 3, float %376)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 4, i32 0, i8 0, float %324)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 4, i32 0, i8 1, float %325)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 4, i32 0, i8 2, float %326)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 0, i32 0, i8 0, float %289)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 0, i32 0, i8 1, float %292)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 0, i32 0, i8 2, float %295)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 0, i32 0, i8 3, float 0.000000e+00)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 1, i32 0, i8 0, float %298)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 1, i32 0, i8 1, float %301)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 1, i32 0, i8 2, float %304)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 1, i32 0, i8 3, float %305)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 2, i32 0, i8 0, float %334)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 2, i32 0, i8 1, float %335)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 2, i32 0, i8 2, float 0.000000e+00)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 2, i32 0, i8 3, float 0.000000e+00)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.i32(i32 5, i32 3, i32 0, i8 0, i32 %88)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
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
declare float @dx.op.unary.f32(i32, float) #0

; Function Attrs: nounwind readnone
declare float @dx.op.dot3.f32(i32, float, float, float, float, float, float) #0

; Function Attrs: nounwind readnone
declare float @dx.op.dot2.f32(i32, float, float, float, float) #0

; Function Attrs: nounwind readnone
declare float @dx.op.tertiary.f32(i32, float, float, float) #0

; Function Attrs: nounwind readnone
declare i32 @dx.op.binary.i32(i32, i32, i32) #0

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
!dx.viewIdState = !{!18}
!dx.entryPoints = !{!19}

!0 = !{!"dxc(private) 1.8.0.0 (private, 00000000)"}
!1 = !{i32 1, i32 6}
!2 = !{i32 1, i32 8}
!3 = !{!"vs", i32 6, i32 6}
!4 = !{!5, null, !14, null}
!5 = !{!6, !8, !10, !11, !13}
!6 = !{i32 0, %"class.StructuredBuffer<unsigned int>"* undef, !"", i32 0, i32 0, i32 1, i32 12, i32 0, !7}
!7 = !{i32 1, i32 4}
!8 = !{i32 1, %"class.StructuredBuffer<vector<float, 4> >"* undef, !"", i32 0, i32 1, i32 1, i32 12, i32 0, !9}
!9 = !{i32 1, i32 16}
!10 = !{i32 2, %"class.StructuredBuffer<vector<float, 4> >"* undef, !"", i32 0, i32 2, i32 1, i32 12, i32 0, !9}
!11 = !{i32 3, %"class.Buffer<vector<float, 2> >"* undef, !"", i32 0, i32 3, i32 1, i32 10, i32 0, !12}
!12 = !{i32 0, i32 9}
!13 = !{i32 4, %"class.Buffer<vector<float, 4> >"* undef, !"", i32 0, i32 4, i32 1, i32 10, i32 0, !12}
!14 = !{!15, !16, !17}
!15 = !{i32 0, %hostlayout.View* undef, !"", i32 0, i32 0, i32 1, i32 10076, null}
!16 = !{i32 1, %Scene* undef, !"", i32 0, i32 1, i32 1, i32 348, null}
!17 = !{i32 2, %LocalVF* undef, !"", i32 0, i32 2, i32 1, i32 60, null}
!18 = !{[15 x i32] [i32 13, i32 24, i32 16187392, i32 16187392, i32 16187392, i32 0, i32 16191735, i32 0, i32 0, i32 0, i32 16191735, i32 0, i32 0, i32 0, i32 1015]}
!19 = !{void ()* @Main, !"Main", !20, !4, !39}
!20 = !{!21, !30, null}
!21 = !{!22, !25, !28, !29}
!22 = !{i32 0, !"ATTRIBUTE", i8 9, i8 0, !23, i8 0, i32 1, i8 4, i32 0, i8 0, !24}
!23 = !{i32 0}
!24 = !{i32 3, i32 7}
!25 = !{i32 1, !"ATTRIBUTE", i8 5, i8 0, !26, i8 0, i32 1, i8 1, i32 1, i8 0, !27}
!26 = !{i32 13}
!27 = !{i32 3, i32 1}
!28 = !{i32 2, !"SV_InstanceID", i8 5, i8 2, !23, i8 0, i32 1, i8 1, i32 2, i8 0, !27}
!29 = !{i32 3, !"SV_VertexID", i8 5, i8 1, !23, i8 0, i32 1, i8 1, i32 3, i8 0, !27}
!30 = !{!31, !33, !34, !35, !36, !38}
!31 = !{i32 0, !"TEXCOORD10_centroid", i8 9, i8 0, !23, i8 2, i32 1, i8 4, i32 0, i8 0, !32}
!32 = !{i32 3, i32 15}
!33 = !{i32 1, !"TEXCOORD11_centroid", i8 9, i8 0, !23, i8 2, i32 1, i8 4, i32 1, i8 0, !32}
!34 = !{i32 2, !"TEXCOORD", i8 9, i8 0, !23, i8 2, i32 1, i8 4, i32 2, i8 0, !32}
!35 = !{i32 3, !"PRIMITIVE_ID", i8 5, i8 0, !23, i8 1, i32 1, i8 1, i32 3, i8 0, !27}
!36 = !{i32 4, !"TEXCOORD", i8 9, i8 0, !37, i8 2, i32 1, i8 3, i32 4, i8 0, !24}
!37 = !{i32 9}
!38 = !{i32 5, !"SV_Position", i8 9, i8 3, !23, i8 4, i32 1, i8 4, i32 5, i8 0, !32}
!39 = !{i32 0, i64 16, i32 5, !23}
!40 = !{i32 1}
!41 = distinct !{!41, !"dx.controlflow.hints", i32 1}
!42 = distinct !{!42, !"dx.controlflow.hints", i32 1}
