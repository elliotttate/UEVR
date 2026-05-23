;
; Input signature:
;
; Name                 Index   Mask Register SysValue  Format   Used
; -------------------- ----- ------ -------- -------- ------- ------
; SV_Position              0   xyzw        0      POS   float   xy  
;
;
; Output signature:
;
; Name                 Index   Mask Register SysValue  Format   Used
; -------------------- ----- ------ -------- -------- ------- ------
; SV_Target                0   xyzw        0   TARGET   float   xyzw
;
; shader debug name: bb8920a77eb3e3eb00b852ba5b7eb51e.pdb
; shader hash: bb8920a77eb3e3eb00b852ba5b7eb51e
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
; SigInputElements: 1
; SigOutputElements: 1
; SigPatchConstOrPrimElements: 0
; SigInputVectors: 1
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
; SV_Position              0          noperspective       
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
;   [260 x i8] (type annotation not present)
;
; }
;
; cbuffer 
; {
;
;   [28 x i8] (type annotation not present)
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
;                                   texture     f32          2d      T0             t0     1
;
;
; ViewId state:
;
; Number of inputs: 4, outputs: 4
; Outputs dependent on ViewId: {  }
; Inputs contributing to computation of Outputs:
;   output 0 depends on inputs: { 0, 1 }
;   output 1 depends on inputs: { 0, 1 }
;   output 2 depends on inputs: { 0, 1 }
;   output 3 depends on inputs: { 0, 1 }
;
target datalayout = "e-m:e-p:32:32-i1:32-i8:32-i16:32-i32:32-i64:64-f16:32-f32:32-f64:64-n8:16:32:64"
target triple = "dxil-ms-dx"

%dx.types.Handle = type { i8* }
%dx.types.ResBind = type { i32, i32, i32, i8 }
%dx.types.ResourceProperties = type { i32, i32 }
%dx.types.CBufRet.i32 = type { i32, i32, i32, i32 }
%dx.types.CBufRet.f32 = type { float, float, float, float }
%dx.types.ResRet.f32 = type { float, float, float, float, i32 }
%"class.Texture2D<vector<float, 4> >" = type { <4 x float>, %"class.Texture2D<vector<float, 4> >::mips_type" }
%"class.Texture2D<vector<float, 4> >::mips_type" = type { i32 }
%"$Globals" = type { <2 x float>, <2 x float>, <2 x float>, <2 x float>, <2 x float>, <2 x float>, <2 x float>, <2 x float>, <2 x float>, <2 x float>, <2 x float>, <2 x float>, <2 x float>, <2 x float>, <2 x float>, <2 x float>, <2 x float>, <2 x float>, <2 x float>, <2 x float>, <2 x float>, <2 x float>, <2 x float>, <2 x float>, <2 x float>, <2 x float>, <2 x float>, <2 x float>, <2 x float>, <2 x float>, <2 x i32>, <2 x float>, i32 }
%Material = type { [1 x <4 x float>], i32, i32, i32 }
%struct.SamplerState = type { i32 }

@offset.i.i.i.i.hca = internal unnamed_addr constant [3 x float] [float 0.000000e+00, float 0x3FF6276280000000, float 0x4009D89D80000000]
@weight.i.i.i.i.hca = internal unnamed_addr constant [3 x float] [float 0x3FCD0F38C0000000, float 0x3FD43CE300000000, float 0x3FB1FD3B80000000]

define void @MainPS() {
  %1 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind zeroinitializer, i32 0, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %2 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 0, i32 0, i32 0, i8 3 }, i32 0, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %3 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 1, i32 1, i32 0, i8 2 }, i32 1, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %4 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 0, i32 0, i32 0, i8 2 }, i32 0, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %5 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %4, %dx.types.ResourceProperties { i32 13, i32 260 })  ; AnnotateHandle(res,props)  resource: CBuffer
  %6 = call float @dx.op.loadInput.f32(i32 4, i32 0, i32 0, i8 0, i32 undef), !dx.precise !24  ; LoadInput(inputSigId,rowIndex,colIndex,gsVertexAxis)
  %7 = call float @dx.op.loadInput.f32(i32 4, i32 0, i32 0, i8 1, i32 undef), !dx.precise !24  ; LoadInput(inputSigId,rowIndex,colIndex,gsVertexAxis)
  %8 = call %dx.types.CBufRet.i32 @dx.op.cbufferLoadLegacy.i32(i32 59, %dx.types.Handle %5, i32 15)  ; CBufferLoadLegacy(handle,regIndex)
  %9 = extractvalue %dx.types.CBufRet.i32 %8, 0
  %10 = extractvalue %dx.types.CBufRet.i32 %8, 1
  %11 = uitofp i32 %9 to float
  %12 = uitofp i32 %10 to float
  %13 = fsub fast float %6, %11
  %14 = fsub fast float %7, %12
  %15 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %5, i32 15)  ; CBufferLoadLegacy(handle,regIndex)
  %16 = extractvalue %dx.types.CBufRet.f32 %15, 2
  %17 = extractvalue %dx.types.CBufRet.f32 %15, 3
  %18 = fmul fast float %13, %16
  %19 = fmul fast float %14, %17
  %20 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %5, i32 1)  ; CBufferLoadLegacy(handle,regIndex)
  %21 = extractvalue %dx.types.CBufRet.f32 %20, 2
  %22 = extractvalue %dx.types.CBufRet.f32 %20, 3
  %23 = fmul fast float %18, %21
  %24 = fmul fast float %19, %22
  %25 = extractvalue %dx.types.CBufRet.f32 %20, 0
  %26 = extractvalue %dx.types.CBufRet.f32 %20, 1
  %27 = fadd fast float %23, %25
  %28 = fadd fast float %24, %26
  %29 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %5, i32 2)  ; CBufferLoadLegacy(handle,regIndex)
  %30 = extractvalue %dx.types.CBufRet.f32 %29, 0
  %31 = extractvalue %dx.types.CBufRet.f32 %29, 1
  %32 = extractvalue %dx.types.CBufRet.f32 %29, 2
  %33 = extractvalue %dx.types.CBufRet.f32 %29, 3
  %34 = call float @dx.op.binary.f32(i32 35, float %27, float %30)  ; FMax(a,b)
  %35 = call float @dx.op.binary.f32(i32 35, float %28, float %31)  ; FMax(a,b)
  %36 = call float @dx.op.binary.f32(i32 36, float %34, float %32)  ; FMin(a,b)
  %37 = call float @dx.op.binary.f32(i32 36, float %35, float %33)  ; FMin(a,b)
  %38 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %1, %dx.types.ResourceProperties { i32 2, i32 1033 })  ; AnnotateHandle(res,props)  resource: Texture2D<4xF32>
  %39 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %2, %dx.types.ResourceProperties { i32 14, i32 0 })  ; AnnotateHandle(res,props)  resource: SamplerState
  %40 = call %dx.types.ResRet.f32 @dx.op.sample.f32(i32 60, %dx.types.Handle %38, %dx.types.Handle %39, float %36, float %37, float undef, float undef, i32 0, i32 0, i32 undef, float undef)  ; Sample(srv,sampler,coord0,coord1,coord2,coord3,offset0,offset1,offset2,clamp)
  %41 = extractvalue %dx.types.ResRet.f32 %40, 0
  %42 = extractvalue %dx.types.ResRet.f32 %40, 1
  %43 = extractvalue %dx.types.ResRet.f32 %40, 2
  %44 = call %dx.types.CBufRet.i32 @dx.op.cbufferLoadLegacy.i32(i32 59, %dx.types.Handle %5, i32 16)  ; CBufferLoadLegacy(handle,regIndex)
  %45 = extractvalue %dx.types.CBufRet.i32 %44, 0
  %46 = icmp eq i32 %45, 14
  %47 = fmul fast float %41, 0x3FCD0F38C0000000
  %48 = fmul fast float %42, 0x3FCD0F38C0000000
  %49 = fmul fast float %43, 0x3FCD0F38C0000000
  br label %50

; <label>:50                                      ; preds = %118, %0
  %51 = phi float [ 0x3FD43CE300000000, %0 ], [ %122, %118 ]
  %52 = phi float [ 0x3FF6276280000000, %0 ], [ %120, %118 ]
  %53 = phi float [ %47, %0 ], [ %113, %118 ]
  %54 = phi float [ %48, %0 ], [ %114, %118 ]
  %55 = phi float [ %49, %0 ], [ %115, %118 ]
  %56 = phi i32 [ 1, %0 ], [ %116, %118 ]
  %57 = call %dx.types.CBufRet.i32 @dx.op.cbufferLoadLegacy.i32(i32 59, %dx.types.Handle %5, i32 15)  ; CBufferLoadLegacy(handle,regIndex)
  %58 = extractvalue %dx.types.CBufRet.i32 %57, 0
  %59 = extractvalue %dx.types.CBufRet.i32 %57, 1
  %60 = uitofp i32 %58 to float
  %61 = uitofp i32 %59 to float
  %62 = fsub fast float %6, %60
  %63 = fsub fast float %7, %61
  %64 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %5, i32 15)  ; CBufferLoadLegacy(handle,regIndex)
  %65 = extractvalue %dx.types.CBufRet.f32 %64, 2
  %66 = extractvalue %dx.types.CBufRet.f32 %64, 3
  %67 = fmul fast float %62, %65
  %68 = fmul fast float %63, %66
  %69 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %5, i32 1)  ; CBufferLoadLegacy(handle,regIndex)
  %70 = extractvalue %dx.types.CBufRet.f32 %69, 2
  %71 = extractvalue %dx.types.CBufRet.f32 %69, 3
  %72 = fmul fast float %67, %70
  %73 = fmul fast float %68, %71
  %74 = extractvalue %dx.types.CBufRet.f32 %69, 0
  %75 = extractvalue %dx.types.CBufRet.f32 %69, 1
  %76 = fadd fast float %72, %74
  %77 = fadd fast float %73, %75
  %78 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %5, i32 0)  ; CBufferLoadLegacy(handle,regIndex)
  %79 = extractvalue %dx.types.CBufRet.f32 %78, 2
  %80 = fmul fast float %79, %52
  %81 = fadd fast float %76, %80
  %82 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %5, i32 2)  ; CBufferLoadLegacy(handle,regIndex)
  %83 = extractvalue %dx.types.CBufRet.f32 %82, 0
  %84 = extractvalue %dx.types.CBufRet.f32 %82, 1
  %85 = extractvalue %dx.types.CBufRet.f32 %82, 2
  %86 = extractvalue %dx.types.CBufRet.f32 %82, 3
  %87 = call float @dx.op.binary.f32(i32 35, float %81, float %83)  ; FMax(a,b)
  %88 = call float @dx.op.binary.f32(i32 35, float %77, float %84)  ; FMax(a,b)
  %89 = call float @dx.op.binary.f32(i32 36, float %87, float %85)  ; FMin(a,b)
  %90 = call float @dx.op.binary.f32(i32 36, float %88, float %86)  ; FMin(a,b)
  %91 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %1, %dx.types.ResourceProperties { i32 2, i32 1033 })  ; AnnotateHandle(res,props)  resource: Texture2D<4xF32>
  %92 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %2, %dx.types.ResourceProperties { i32 14, i32 0 })  ; AnnotateHandle(res,props)  resource: SamplerState
  %93 = call %dx.types.ResRet.f32 @dx.op.sample.f32(i32 60, %dx.types.Handle %91, %dx.types.Handle %92, float %89, float %90, float undef, float undef, i32 0, i32 0, i32 undef, float undef)  ; Sample(srv,sampler,coord0,coord1,coord2,coord3,offset0,offset1,offset2,clamp)
  %94 = extractvalue %dx.types.ResRet.f32 %93, 0
  %95 = extractvalue %dx.types.ResRet.f32 %93, 1
  %96 = extractvalue %dx.types.ResRet.f32 %93, 2
  %97 = fmul fast float %94, %51
  %98 = fmul fast float %95, %51
  %99 = fmul fast float %96, %51
  %100 = fadd fast float %97, %53
  %101 = fadd fast float %98, %54
  %102 = fadd fast float %99, %55
  %103 = fsub fast float %76, %80
  %104 = call float @dx.op.binary.f32(i32 35, float %103, float %83)  ; FMax(a,b)
  %105 = call float @dx.op.binary.f32(i32 36, float %104, float %85)  ; FMin(a,b)
  %106 = call %dx.types.ResRet.f32 @dx.op.sample.f32(i32 60, %dx.types.Handle %91, %dx.types.Handle %92, float %105, float %90, float undef, float undef, i32 0, i32 0, i32 undef, float undef)  ; Sample(srv,sampler,coord0,coord1,coord2,coord3,offset0,offset1,offset2,clamp)
  %107 = extractvalue %dx.types.ResRet.f32 %106, 0
  %108 = extractvalue %dx.types.ResRet.f32 %106, 1
  %109 = extractvalue %dx.types.ResRet.f32 %106, 2
  %110 = fmul fast float %107, %51
  %111 = fmul fast float %108, %51
  %112 = fmul fast float %109, %51
  %113 = fadd fast float %100, %110
  %114 = fadd fast float %101, %111
  %115 = fadd fast float %102, %112
  %116 = add nuw nsw i32 %56, 1
  %117 = icmp eq i32 %116, 3
  br i1 %117, label %123, label %118

; <label>:118                                     ; preds = %50
  %119 = getelementptr inbounds [3 x float], [3 x float]* @offset.i.i.i.i.hca, i32 0, i32 %116
  %120 = load float, float* %119, align 4, !tbaa !25, !noalias !29
  %121 = getelementptr inbounds [3 x float], [3 x float]* @weight.i.i.i.i.hca, i32 0, i32 %116
  %122 = load float, float* %121, align 4, !tbaa !25, !noalias !29
  br label %50

; <label>:123                                     ; preds = %50
  %124 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %3, %dx.types.ResourceProperties { i32 13, i32 28 })  ; AnnotateHandle(res,props)  resource: CBuffer
  %125 = extractvalue %dx.types.ResRet.f32 %40, 3
  %126 = select i1 %46, float %125, float 0.000000e+00
  %127 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %124, i32 0)  ; CBufferLoadLegacy(handle,regIndex)
  %128 = extractvalue %dx.types.CBufRet.f32 %127, 0
  %129 = extractvalue %dx.types.CBufRet.f32 %127, 1
  %130 = extractvalue %dx.types.CBufRet.f32 %127, 2
  %131 = extractvalue %dx.types.CBufRet.f32 %127, 3
  %132 = fsub fast float %129, %113
  %133 = fsub fast float %130, %114
  %134 = fsub fast float %131, %115
  %135 = fmul fast float %132, %128
  %136 = fmul fast float %133, %128
  %137 = fmul fast float %134, %128
  %138 = fadd fast float %135, %113
  %139 = fadd fast float %136, %114
  %140 = fadd fast float %137, %115
  %141 = call float @dx.op.binary.f32(i32 35, float %138, float 0.000000e+00)  ; FMax(a,b)
  %142 = call float @dx.op.binary.f32(i32 35, float %139, float 0.000000e+00)  ; FMax(a,b)
  %143 = call float @dx.op.binary.f32(i32 35, float %140, float 0.000000e+00)  ; FMax(a,b)
  call void @dx.op.storeOutput.f32(i32 5, i32 0, i32 0, i8 0, float %141)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 0, i32 0, i8 1, float %142)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 0, i32 0, i8 2, float %143)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 0, i32 0, i8 3, float %126)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  ret void
}

; Function Attrs: nounwind readnone
declare float @dx.op.loadInput.f32(i32, i32, i32, i8, i32) #0

; Function Attrs: nounwind
declare void @dx.op.storeOutput.f32(i32, i32, i32, i8, float) #1

; Function Attrs: nounwind readnone
declare float @dx.op.binary.f32(i32, float, float) #0

; Function Attrs: nounwind readonly
declare %dx.types.ResRet.f32 @dx.op.sample.f32(i32, %dx.types.Handle, %dx.types.Handle, float, float, float, float, i32, i32, i32, float) #2

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
!dx.viewIdState = !{!13}
!dx.entryPoints = !{!14}

!0 = !{!"dxc(private) 1.8.0.0 (private, 00000000)"}
!1 = !{i32 1, i32 6}
!2 = !{i32 1, i32 8}
!3 = !{!"ps", i32 6, i32 6}
!4 = !{!5, null, !8, !11}
!5 = !{!6}
!6 = !{i32 0, %"class.Texture2D<vector<float, 4> >"* undef, !"", i32 0, i32 0, i32 1, i32 2, i32 0, !7}
!7 = !{i32 0, i32 9}
!8 = !{!9, !10}
!9 = !{i32 0, %"$Globals"* undef, !"", i32 0, i32 0, i32 1, i32 260, null}
!10 = !{i32 1, %Material* undef, !"", i32 0, i32 1, i32 1, i32 28, null}
!11 = !{!12}
!12 = !{i32 0, %struct.SamplerState* undef, !"", i32 0, i32 0, i32 1, i32 0, null}
!13 = !{[6 x i32] [i32 4, i32 4, i32 15, i32 15, i32 0, i32 0]}
!14 = !{void ()* @MainPS, !"MainPS", !15, !4, !23}
!15 = !{!16, !20, null}
!16 = !{!17}
!17 = !{i32 0, !"SV_Position", i8 9, i8 3, !18, i8 4, i32 1, i8 4, i32 0, i8 0, !19}
!18 = !{i32 0}
!19 = !{i32 3, i32 3}
!20 = !{!21}
!21 = !{i32 0, !"SV_Target", i8 9, i8 16, !18, i8 0, i32 1, i8 4, i32 0, i8 0, !22}
!22 = !{i32 3, i32 15}
!23 = !{i32 5, !18}
!24 = !{i32 1}
!25 = !{!26, !26, i64 0}
!26 = !{!"float", !27, i64 0}
!27 = !{!"omnipotent char", !28, i64 0}
!28 = !{!"Simple C/C++ TBAA"}
!29 = !{!30, !32, !33, !35, !36, !38}
!30 = distinct !{!30, !31, !"\01?CalcPixelMaterialInputs@@YAXUFMaterialPixelParameters@@UFPixelMaterialInputs@@@Z: %Parameters"}
!31 = distinct !{!31, !"\01?CalcPixelMaterialInputs@@YAXUFMaterialPixelParameters@@UFPixelMaterialInputs@@@Z"}
!32 = distinct !{!32, !31, !"\01?CalcPixelMaterialInputs@@YAXUFMaterialPixelParameters@@UFPixelMaterialInputs@@@Z: %PixelMaterialInputs"}
!33 = distinct !{!33, !34, !"\01?CalcMaterialParametersEx@@YAXUFMaterialPixelParameters@@UFPixelMaterialInputs@@V?$vector@M$03@@2_NV?$vector@M$02@@4@Z: %Parameters"}
!34 = distinct !{!34, !"\01?CalcMaterialParametersEx@@YAXUFMaterialPixelParameters@@UFPixelMaterialInputs@@V?$vector@M$03@@2_NV?$vector@M$02@@4@Z"}
!35 = distinct !{!35, !34, !"\01?CalcMaterialParametersEx@@YAXUFMaterialPixelParameters@@UFPixelMaterialInputs@@V?$vector@M$03@@2_NV?$vector@M$02@@4@Z: %PixelMaterialInputs"}
!36 = distinct !{!36, !37, !"\01?CalcMaterialParametersPost@@YAXUFMaterialPixelParameters@@UFPixelMaterialInputs@@V?$vector@M$03@@_N@Z: %Parameters"}
!37 = distinct !{!37, !"\01?CalcMaterialParametersPost@@YAXUFMaterialPixelParameters@@UFPixelMaterialInputs@@V?$vector@M$03@@_N@Z"}
!38 = distinct !{!38, !37, !"\01?CalcMaterialParametersPost@@YAXUFMaterialPixelParameters@@UFPixelMaterialInputs@@V?$vector@M$03@@_N@Z: %PixelMaterialInputs"}
