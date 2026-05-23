;
; Input signature:
;
; Name                 Index   Mask Register SysValue  Format   Used
; -------------------- ----- ------ -------- -------- ------- ------
; TEXCOORD                 0   xyzw        0     NONE   float   xy  
;
;
; Output signature:
;
; Name                 Index   Mask Register SysValue  Format   Used
; -------------------- ----- ------ -------- -------- ------- ------
; SV_Target                0   xyzw        0   TARGET   float   xyzw
;
; shader debug name: 2e87e1a33a2dca29890d2b31a4dcbf54.pdb
; shader hash: 2e87e1a33a2dca29890d2b31a4dcbf54
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
; EntryFunctionName: GaussianBlurMain
;
;
; Input signature:
;
; Name                 Index             InterpMode DynIdx
; -------------------- ----- ---------------------- ------
; TEXCOORD                 0          noperspective       
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
;   [1072 x i8] (type annotation not present)
;
; }
;
;
; Resource Bindings:
;
; Name                                 Type  Format         Dim      ID      HLSL Bind  Count
; ------------------------------ ---------- ------- ----------- ------- -------------- ------
;                                   cbuffer      NA          NA     CB0            cb0     1
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
;
target datalayout = "e-m:e-p:32:32-i1:32-i8:32-i16:32-i32:32-i64:64-f16:32-f32:32-f64:64-n8:16:32:64"
target triple = "dxil-ms-dx"

%dx.types.Handle = type { i8* }
%dx.types.ResBind = type { i32, i32, i32, i8 }
%dx.types.ResourceProperties = type { i32, i32 }
%dx.types.CBufRet.f32 = type { float, float, float, float }
%dx.types.ResRet.f32 = type { float, float, float, float, i32 }
%dx.types.CBufRet.i32 = type { i32, i32, i32, i32 }
%"class.Texture2D<vector<float, 4> >" = type { <4 x float>, %"class.Texture2D<vector<float, 4> >::mips_type" }
%"class.Texture2D<vector<float, 4> >::mips_type" = type { i32 }
%_RootShaderParameters = type { [63 x <4 x float>], i32, <4 x float>, <4 x float> }
%struct.SamplerState = type { i32 }

define void @GaussianBlurMain() {
  %1 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind zeroinitializer, i32 0, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %2 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 0, i32 0, i32 0, i8 3 }, i32 0, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %3 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 0, i32 0, i32 0, i8 2 }, i32 0, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %4 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %3, %dx.types.ResourceProperties { i32 13, i32 1072 })  ; AnnotateHandle(res,props)  resource: CBuffer
  %5 = call float @dx.op.loadInput.f32(i32 4, i32 0, i32 0, i8 0, i32 undef)  ; LoadInput(inputSigId,rowIndex,colIndex,gsVertexAxis)
  %6 = call float @dx.op.loadInput.f32(i32 4, i32 0, i32 0, i8 1, i32 undef)  ; LoadInput(inputSigId,rowIndex,colIndex,gsVertexAxis)
  %7 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %4, i32 66)  ; CBufferLoadLegacy(handle,regIndex)
  %8 = extractvalue %dx.types.CBufRet.f32 %7, 2
  %9 = extractvalue %dx.types.CBufRet.f32 %7, 3
  %10 = extractvalue %dx.types.CBufRet.f32 %7, 0
  %11 = extractvalue %dx.types.CBufRet.f32 %7, 1
  %12 = call float @dx.op.binary.f32(i32 35, float %5, float %10)  ; FMax(a,b)
  %13 = call float @dx.op.binary.f32(i32 35, float %6, float %11)  ; FMax(a,b)
  %14 = call float @dx.op.binary.f32(i32 36, float %12, float %8)  ; FMin(a,b)
  %15 = call float @dx.op.binary.f32(i32 36, float %13, float %9)  ; FMin(a,b)
  %16 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %1, %dx.types.ResourceProperties { i32 2, i32 1033 })  ; AnnotateHandle(res,props)  resource: Texture2D<4xF32>
  %17 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %2, %dx.types.ResourceProperties { i32 14, i32 0 })  ; AnnotateHandle(res,props)  resource: SamplerState
  %18 = call %dx.types.ResRet.f32 @dx.op.sample.f32(i32 60, %dx.types.Handle %16, %dx.types.Handle %17, float %14, float %15, float undef, float undef, i32 0, i32 0, i32 undef, float undef)  ; Sample(srv,sampler,coord0,coord1,coord2,coord3,offset0,offset1,offset2,clamp)
  %19 = extractvalue %dx.types.ResRet.f32 %18, 0
  %20 = extractvalue %dx.types.ResRet.f32 %18, 1
  %21 = extractvalue %dx.types.ResRet.f32 %18, 2
  %22 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %4, i32 1)  ; CBufferLoadLegacy(handle,regIndex)
  %23 = extractvalue %dx.types.CBufRet.f32 %22, 0
  %24 = fmul fast float %23, %19
  %25 = fmul fast float %23, %20
  %26 = fmul fast float %23, %21
  %27 = extractvalue %dx.types.CBufRet.f32 %22, 2
  %28 = extractvalue %dx.types.CBufRet.f32 %22, 3
  %29 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %4, i32 66)  ; CBufferLoadLegacy(handle,regIndex)
  %30 = extractvalue %dx.types.CBufRet.f32 %29, 0
  %31 = extractvalue %dx.types.CBufRet.f32 %29, 1
  %32 = extractvalue %dx.types.CBufRet.f32 %29, 2
  %33 = extractvalue %dx.types.CBufRet.f32 %29, 3
  %34 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %4, i32 65)  ; CBufferLoadLegacy(handle,regIndex)
  %35 = extractvalue %dx.types.CBufRet.f32 %34, 2
  %36 = extractvalue %dx.types.CBufRet.f32 %34, 3
  %37 = extractvalue %dx.types.CBufRet.f32 %34, 0
  %38 = extractvalue %dx.types.CBufRet.f32 %34, 1
  %39 = fmul fast float %35, %28
  %40 = fmul fast float %39, %37
  %41 = fmul fast float %36, %28
  %42 = fmul fast float %41, %38
  %43 = fadd fast float %40, %5
  %44 = fadd fast float %42, %6
  %45 = call float @dx.op.binary.f32(i32 35, float %43, float %30)  ; FMax(a,b)
  %46 = call float @dx.op.binary.f32(i32 35, float %44, float %31)  ; FMax(a,b)
  %47 = call float @dx.op.binary.f32(i32 36, float %45, float %32)  ; FMin(a,b)
  %48 = call float @dx.op.binary.f32(i32 36, float %46, float %33)  ; FMin(a,b)
  %49 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %1, %dx.types.ResourceProperties { i32 2, i32 1033 })  ; AnnotateHandle(res,props)  resource: Texture2D<4xF32>
  %50 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %2, %dx.types.ResourceProperties { i32 14, i32 0 })  ; AnnotateHandle(res,props)  resource: SamplerState
  %51 = call %dx.types.ResRet.f32 @dx.op.sample.f32(i32 60, %dx.types.Handle %49, %dx.types.Handle %50, float %47, float %48, float undef, float undef, i32 0, i32 0, i32 undef, float undef)  ; Sample(srv,sampler,coord0,coord1,coord2,coord3,offset0,offset1,offset2,clamp)
  %52 = extractvalue %dx.types.ResRet.f32 %51, 0
  %53 = extractvalue %dx.types.ResRet.f32 %51, 1
  %54 = extractvalue %dx.types.ResRet.f32 %51, 2
  %55 = fsub fast float %5, %40
  %56 = fsub fast float %6, %42
  %57 = call float @dx.op.binary.f32(i32 35, float %55, float %30)  ; FMax(a,b)
  %58 = call float @dx.op.binary.f32(i32 35, float %56, float %31)  ; FMax(a,b)
  %59 = call float @dx.op.binary.f32(i32 36, float %57, float %32)  ; FMin(a,b)
  %60 = call float @dx.op.binary.f32(i32 36, float %58, float %33)  ; FMin(a,b)
  %61 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %1, %dx.types.ResourceProperties { i32 2, i32 1033 })  ; AnnotateHandle(res,props)  resource: Texture2D<4xF32>
  %62 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %2, %dx.types.ResourceProperties { i32 14, i32 0 })  ; AnnotateHandle(res,props)  resource: SamplerState
  %63 = call %dx.types.ResRet.f32 @dx.op.sample.f32(i32 60, %dx.types.Handle %61, %dx.types.Handle %62, float %59, float %60, float undef, float undef, i32 0, i32 0, i32 undef, float undef)  ; Sample(srv,sampler,coord0,coord1,coord2,coord3,offset0,offset1,offset2,clamp)
  %64 = extractvalue %dx.types.ResRet.f32 %63, 0
  %65 = extractvalue %dx.types.ResRet.f32 %63, 1
  %66 = extractvalue %dx.types.ResRet.f32 %63, 2
  %67 = fadd fast float %64, %52
  %68 = fmul fast float %67, %27
  %69 = fadd fast float %65, %53
  %70 = fmul fast float %69, %27
  %71 = fadd fast float %66, %54
  %72 = fmul fast float %71, %27
  %73 = fadd fast float %68, %24
  %74 = fadd fast float %70, %25
  %75 = fadd fast float %72, %26
  %76 = call %dx.types.CBufRet.i32 @dx.op.cbufferLoadLegacy.i32(i32 59, %dx.types.Handle %4, i32 64)  ; CBufferLoadLegacy(handle,regIndex)
  %77 = extractvalue %dx.types.CBufRet.i32 %76, 0
  %78 = icmp sgt i32 %77, 2
  br i1 %78, label %79, label %175

; <label>:79                                      ; preds = %0
  br label %80

; <label>:80                                      ; preds = %80, %79
  %81 = phi float [ %167, %80 ], [ %73, %79 ]
  %82 = phi float [ %168, %80 ], [ %74, %79 ]
  %83 = phi float [ %169, %80 ], [ %75, %79 ]
  %84 = phi i32 [ %170, %80 ], [ 2, %79 ]
  %85 = sdiv i32 %84, 2
  %86 = add nsw i32 %85, 1
  %87 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %4, i32 %86)  ; CBufferLoadLegacy(handle,regIndex)
  %88 = extractvalue %dx.types.CBufRet.f32 %87, 0
  %89 = extractvalue %dx.types.CBufRet.f32 %87, 1
  %90 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %4, i32 66)  ; CBufferLoadLegacy(handle,regIndex)
  %91 = extractvalue %dx.types.CBufRet.f32 %90, 0
  %92 = extractvalue %dx.types.CBufRet.f32 %90, 1
  %93 = extractvalue %dx.types.CBufRet.f32 %90, 2
  %94 = extractvalue %dx.types.CBufRet.f32 %90, 3
  %95 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %4, i32 65)  ; CBufferLoadLegacy(handle,regIndex)
  %96 = extractvalue %dx.types.CBufRet.f32 %95, 2
  %97 = extractvalue %dx.types.CBufRet.f32 %95, 3
  %98 = extractvalue %dx.types.CBufRet.f32 %95, 0
  %99 = extractvalue %dx.types.CBufRet.f32 %95, 1
  %100 = fmul fast float %96, %89
  %101 = fmul fast float %100, %98
  %102 = fmul fast float %97, %89
  %103 = fmul fast float %102, %99
  %104 = fadd fast float %101, %5
  %105 = fadd fast float %103, %6
  %106 = call float @dx.op.binary.f32(i32 35, float %104, float %91)  ; FMax(a,b)
  %107 = call float @dx.op.binary.f32(i32 35, float %105, float %92)  ; FMax(a,b)
  %108 = call float @dx.op.binary.f32(i32 36, float %106, float %93)  ; FMin(a,b)
  %109 = call float @dx.op.binary.f32(i32 36, float %107, float %94)  ; FMin(a,b)
  %110 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %1, %dx.types.ResourceProperties { i32 2, i32 1033 })  ; AnnotateHandle(res,props)  resource: Texture2D<4xF32>
  %111 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %2, %dx.types.ResourceProperties { i32 14, i32 0 })  ; AnnotateHandle(res,props)  resource: SamplerState
  %112 = call %dx.types.ResRet.f32 @dx.op.sample.f32(i32 60, %dx.types.Handle %110, %dx.types.Handle %111, float %108, float %109, float undef, float undef, i32 0, i32 0, i32 undef, float undef)  ; Sample(srv,sampler,coord0,coord1,coord2,coord3,offset0,offset1,offset2,clamp)
  %113 = extractvalue %dx.types.ResRet.f32 %112, 0
  %114 = extractvalue %dx.types.ResRet.f32 %112, 1
  %115 = extractvalue %dx.types.ResRet.f32 %112, 2
  %116 = fsub fast float %5, %101
  %117 = fsub fast float %6, %103
  %118 = call float @dx.op.binary.f32(i32 35, float %116, float %91)  ; FMax(a,b)
  %119 = call float @dx.op.binary.f32(i32 35, float %117, float %92)  ; FMax(a,b)
  %120 = call float @dx.op.binary.f32(i32 36, float %118, float %93)  ; FMin(a,b)
  %121 = call float @dx.op.binary.f32(i32 36, float %119, float %94)  ; FMin(a,b)
  %122 = call %dx.types.ResRet.f32 @dx.op.sample.f32(i32 60, %dx.types.Handle %110, %dx.types.Handle %111, float %120, float %121, float undef, float undef, i32 0, i32 0, i32 undef, float undef)  ; Sample(srv,sampler,coord0,coord1,coord2,coord3,offset0,offset1,offset2,clamp)
  %123 = extractvalue %dx.types.ResRet.f32 %122, 0
  %124 = extractvalue %dx.types.ResRet.f32 %122, 1
  %125 = extractvalue %dx.types.ResRet.f32 %122, 2
  %126 = fadd fast float %123, %113
  %127 = fmul fast float %126, %88
  %128 = fadd fast float %124, %114
  %129 = fmul fast float %128, %88
  %130 = fadd fast float %125, %115
  %131 = fmul fast float %130, %88
  %132 = fadd fast float %127, %81
  %133 = fadd fast float %129, %82
  %134 = fadd fast float %131, %83
  %135 = extractvalue %dx.types.CBufRet.f32 %87, 2
  %136 = extractvalue %dx.types.CBufRet.f32 %87, 3
  %137 = fmul fast float %96, %136
  %138 = fmul fast float %137, %98
  %139 = fmul fast float %97, %136
  %140 = fmul fast float %139, %99
  %141 = fadd fast float %138, %5
  %142 = fadd fast float %140, %6
  %143 = call float @dx.op.binary.f32(i32 35, float %141, float %91)  ; FMax(a,b)
  %144 = call float @dx.op.binary.f32(i32 35, float %142, float %92)  ; FMax(a,b)
  %145 = call float @dx.op.binary.f32(i32 36, float %143, float %93)  ; FMin(a,b)
  %146 = call float @dx.op.binary.f32(i32 36, float %144, float %94)  ; FMin(a,b)
  %147 = call %dx.types.ResRet.f32 @dx.op.sample.f32(i32 60, %dx.types.Handle %110, %dx.types.Handle %111, float %145, float %146, float undef, float undef, i32 0, i32 0, i32 undef, float undef)  ; Sample(srv,sampler,coord0,coord1,coord2,coord3,offset0,offset1,offset2,clamp)
  %148 = extractvalue %dx.types.ResRet.f32 %147, 0
  %149 = extractvalue %dx.types.ResRet.f32 %147, 1
  %150 = extractvalue %dx.types.ResRet.f32 %147, 2
  %151 = fsub fast float %5, %138
  %152 = fsub fast float %6, %140
  %153 = call float @dx.op.binary.f32(i32 35, float %151, float %91)  ; FMax(a,b)
  %154 = call float @dx.op.binary.f32(i32 35, float %152, float %92)  ; FMax(a,b)
  %155 = call float @dx.op.binary.f32(i32 36, float %153, float %93)  ; FMin(a,b)
  %156 = call float @dx.op.binary.f32(i32 36, float %154, float %94)  ; FMin(a,b)
  %157 = call %dx.types.ResRet.f32 @dx.op.sample.f32(i32 60, %dx.types.Handle %110, %dx.types.Handle %111, float %155, float %156, float undef, float undef, i32 0, i32 0, i32 undef, float undef)  ; Sample(srv,sampler,coord0,coord1,coord2,coord3,offset0,offset1,offset2,clamp)
  %158 = extractvalue %dx.types.ResRet.f32 %157, 0
  %159 = extractvalue %dx.types.ResRet.f32 %157, 1
  %160 = extractvalue %dx.types.ResRet.f32 %157, 2
  %161 = fadd fast float %158, %148
  %162 = fmul fast float %161, %135
  %163 = fadd fast float %159, %149
  %164 = fmul fast float %163, %135
  %165 = fadd fast float %160, %150
  %166 = fmul fast float %165, %135
  %167 = fadd fast float %132, %162
  %168 = fadd fast float %133, %164
  %169 = fadd fast float %134, %166
  %170 = add nuw nsw i32 %84, 2
  %171 = call %dx.types.CBufRet.i32 @dx.op.cbufferLoadLegacy.i32(i32 59, %dx.types.Handle %4, i32 64)  ; CBufferLoadLegacy(handle,regIndex)
  %172 = extractvalue %dx.types.CBufRet.i32 %171, 0
  %173 = icmp slt i32 %170, %172
  br i1 %173, label %80, label %174

; <label>:174                                     ; preds = %80
  br label %175

; <label>:175                                     ; preds = %174, %0
  %176 = phi float [ %73, %0 ], [ %167, %174 ]
  %177 = phi float [ %74, %0 ], [ %168, %174 ]
  %178 = phi float [ %75, %0 ], [ %169, %174 ]
  call void @dx.op.storeOutput.f32(i32 5, i32 0, i32 0, i8 0, float %176)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 0, i32 0, i8 1, float %177)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 0, i32 0, i8 2, float %178)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 0, i32 0, i8 3, float 1.000000e+00)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
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
!dx.viewIdState = !{!12}
!dx.entryPoints = !{!13}

!0 = !{!"dxc(private) 1.8.0.0 (private, 00000000)"}
!1 = !{i32 1, i32 6}
!2 = !{i32 1, i32 8}
!3 = !{!"ps", i32 6, i32 6}
!4 = !{!5, null, !8, !10}
!5 = !{!6}
!6 = !{i32 0, %"class.Texture2D<vector<float, 4> >"* undef, !"", i32 0, i32 0, i32 1, i32 2, i32 0, !7}
!7 = !{i32 0, i32 9}
!8 = !{!9}
!9 = !{i32 0, %_RootShaderParameters* undef, !"", i32 0, i32 0, i32 1, i32 1072, null}
!10 = !{!11}
!11 = !{i32 0, %struct.SamplerState* undef, !"", i32 0, i32 0, i32 1, i32 0, null}
!12 = !{[6 x i32] [i32 4, i32 4, i32 7, i32 7, i32 0, i32 0]}
!13 = !{void ()* @GaussianBlurMain, !"GaussianBlurMain", !14, !4, !22}
!14 = !{!15, !19, null}
!15 = !{!16}
!16 = !{i32 0, !"TEXCOORD", i8 9, i8 0, !17, i8 4, i32 1, i8 4, i32 0, i8 0, !18}
!17 = !{i32 0}
!18 = !{i32 3, i32 3}
!19 = !{!20}
!20 = !{i32 0, !"SV_Target", i8 9, i8 16, !17, i8 0, i32 1, i8 4, i32 0, i8 0, !21}
!21 = !{i32 3, i32 15}
!22 = !{i32 5, !17}
