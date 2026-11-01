import { DemoL10n } from '../ui/DemoL10n';
import { FaceVerificationFlow } from '../verification/FaceVerificationFlow';
import { FaceLivenessFlow } from '../liveness/FaceLivenessFlow';
import { SearchSettingsDialog } from '../search/SearchSettingsDialog';
import { SearchScreenPresentation } from '../search/SearchScreenPresentation';
import { createDemoFaceSearchConfig } from '../search/DemoFaceSearchConfig';
import { DemoTheme } from '../ui/DemoTheme';
import { RealtimeMode } from '../camera/RealtimeMode';
import { DemoCameraCallbacks } from '../camera/DemoCameraCallbacks';
import { DemoFlowListener } from '../ui/DemoFlowListener';
import { DemoSearchListener } from '../search/DemoSearchListener';
import { FaceDetectionOverlay } from '../detection/FaceDetectionOverlay';
import { FaceCaptureOverlay } from '../ui/FaceCaptureOverlay';
import { FaceSearchOverlay } from '../search/FaceSearchOverlay';
import { FaceSearchOverlayState } from '../search/FaceSearchOverlayState';
import { FaceSearchOverlayExpirySchedule } from '../search/FaceSearchOverlayExpirySchedule';
import { FaceFlowUi } from '../ui/FaceFlowUi';
import { FaceSearchStatusUi } from '../search/FaceSearchStatusUi';
import { DemoErrorUi } from '../ui/DemoErrorUi';
import { common } from '@kit.AbilityKit';
import { systemDateTime } from '@kit.BasicServicesKit';
import { image } from '@kit.ImageKit';
import { hilog } from '@kit.PerformanceAnalysisKit';
import {
  FaceAction,
  FaceDetectionConfig,
  FaceDetectionResult,
  FaceFlowListener,
  FaceFlowProgress,
  FaceFlowResult,
  FaceFlowStage,
  FaceRealtimeProcessor,
  FaceRequest,
  FaceSearchSessionConfig,
  FaceSearchDatabase,
  FaceSearchFrame,
  FaceSearchListener,
  FaceSearchMode,
  FaceSearchProgress,
  FaceSearchResult,
  FaceSearchSession,
  FaceSearchStage,
  HiFaceError,
  HiFaceErrorCode,
  HiFaceResult,
  HiFaceSdk,
  Yuv420Frame
} from 'hiface';
import { CameraFlowStartQueue } from '../camera/CameraFlowStartQueue';
import { CameraDemoSettings, CameraDemoSettingsStore } from '../camera/CameraDemoSettings';
import { CameraPageLifecycle } from '../camera/CameraPageLifecycle';
import { CameraScreenPresentation } from '../camera/CameraScreenPresentation';
import { FaceChallenge } from '../ui/FaceChallenge';
import { CameraPreviewController } from '../camera/CameraPreviewController';
import { CameraPreviewCallbacks, CameraPreviewState } from '../camera/CameraPreviewCallbacks';
import { CameraPreviewBounds, CameraPreviewLayout } from '../camera/CameraPreviewLayout';
import { PixelMapResource } from '../ui/PixelMapResource';
import { FrameCallback, KeyboardAvoidMode } from '@kit.ArkUI';
import { DEBUG } from 'BuildProfile';

const ACTION_TIMEOUT_MILLIS: number = 3000;
const CAPTURE_STABLE_COUNTDOWN_MILLIS: number = 700;
const CAMERA_LOG_DOMAIN: number = 0xFF00;
const CAMERA_LOG_TAG: string = 'HiFaceCamera';
const SEARCH_ACTIVE_FPS: number = 15;
const SEARCH_IDLE_FPS: number = 10;
const SEARCH_IDLE_ANALYSIS_FPS: number = 5;

class OverlayFrameCallback extends FrameCallback {
  private readonly draw: () => void;

  constructor(draw: () => void) {
    super();
    this.draw = draw;
  }

  onFrame(_frameTimeInNano: number): void {
    this.draw();
  }
}

/**
 * Shared ArkUI host for Android's real-time Activities. Centralize XComponent, CameraKit
 * startup/cleanup, and lifecycle here; feature directories retain detection drawing, search
 * presentation, and verification/liveness configuration. This avoids duplicating asynchronous Surface
 * cleanup across screens.
 *
 * Android 各实时 Activity 的共享 ArkUI 宿主。
 *
 * 本组件集中 XComponent、CameraKit 启停和页面生命周期；检测绘制、搜索展示、验证/活体配置仍在
 * 对应功能目录维护。共享宿主避免多个鸿蒙页面复制异步 Surface 释放逻辑。
 */
@Component
export struct RealtimeCameraDemo {
  @Require
  // The SDK is a shared service reference; @Prop deep-copies internal Promises and breaks startup queues
  // and session state.
  // SDK 是共享服务引用；@Prop 会深拷贝内部 Promise，破坏启停队列和会话状态。
  sdk: HiFaceSdk;
  @Prop initialMode: RealtimeMode = RealtimeMode.DETECTION;
  @Prop initialReferenceFeature: string = '';
  @Prop initialSearchConfig: FaceSearchSessionConfig = createDemoFaceSearchConfig();
  @Prop referenceFaceId: string = '';
  private readonly searchScreenPresentation: SearchScreenPresentation = new SearchScreenPresentation();
  private readonly cameraScreenPresentation: CameraScreenPresentation = new CameraScreenPresentation();
  onReferenceConfirmed: (faceId: string, feature: string) => Promise<boolean> =
    async (): Promise<boolean> => false;
  onEnroll: () => void = () => {};
  onSearchConfigChanged: (config: FaceSearchSessionConfig) => void = () => {};
  cameraLifecycle: CameraPageLifecycle = new CameraPageLifecycle();
  onExit: () => void = () => {};
  onOpenLibrary: () => void = () => {};
  @State private detectedPeople: number = 0;
  @State private searchedPeople: number = 0;
  @State private resultState: string = DemoL10n.text("启动相机后将持续检测人脸");
  @State private guideState: string = DemoL10n.text("实时检测");
  @State private searchResultText: string = '';
  @State private searchMatchCaption: string = '';
  @State private frontCamera: boolean = true;
  @State private currentMode: RealtimeMode = RealtimeMode.DETECTION;
  @State private capturedFace: image.PixelMap | undefined = undefined;
  @State private capturedFaceMirrored: boolean = false;
  @State private flowBusy: boolean = false;
  private flowActive: boolean = false;
  @State private switchingCamera: boolean = false;
  @State private cameraReady: boolean = false;
  @State private cameraFailed: boolean = false;
  @State private cameraError: string = '';
  @State private captureAwaitingConfirmation: boolean = false;
  @State private searchConfiguration: FaceSearchSessionConfig = createDemoFaceSearchConfig();
  @State private searchSettingsVisible: boolean = false;
  @State private searchRetryVisible: boolean = false;
  @State private cameraEnrollmentFaceId: string = '';
  @State private resultVisible: boolean = false;
  @State private resultRetryVisible: boolean = false;
  @State private enrollmentError: string = '';
  @State private cameraEnrollmentGroup: string = '';
  @State private libraryBusy: boolean = false;
  @State private dialogViewportHeight: number = 600;
  private previousKeyboardAvoidMode: KeyboardAvoidMode = KeyboardAvoidMode.OFFSET;
  private readonly xComponentController: XComponentController = new XComponentController();
  private readonly canvasSettings: RenderingContextSettings = new RenderingContextSettings(true);
  private readonly canvasContext: CanvasRenderingContext2D =
    new CanvasRenderingContext2D(this.canvasSettings);
  private readonly flowDetectionConfig: FaceDetectionConfig = FaceDetectionConfig.defaultConfig();
  private readonly detectionConfig: FaceDetectionConfig = new FaceDetectionConfig(10, true);
  private cameraController: CameraPreviewController | undefined = undefined;
  private realtimeProcessor: FaceRealtimeProcessor | undefined = undefined;
  private activeRequest: FaceRequest | undefined = undefined;
  private activeSearchSession: FaceSearchSession | undefined = undefined;
  private enrollmentDatabaseConnection: FaceSearchDatabase | undefined = undefined;
  private referenceFeature: string | undefined = undefined;
  private pendingReferenceFeature: string | undefined = undefined;
  private lastDetection: FaceDetectionResult | undefined = undefined;
  private readonly searchOverlayState: FaceSearchOverlayState = new FaceSearchOverlayState();
  @State private previewWidth: number = 0;
  @State private previewHeight: number = 0;
  private visible: boolean = false;
  private flowGeneration: number = 0;
  private captureGeneration: number = 0;
  private cameraSurfaceGeneration: number = 0;
  private libraryGeneration: number = 0;
  private mirrorCoordinatesCorrection: boolean = false;
  private searchPausedForSettings: boolean = false;
  private lastSearchError: HiFaceError | undefined = undefined;
  private surfaceAvailable: boolean = false;
  private lifecycleBinding: number = 0;
  @State private previewAspect: number = 0.75;
  private countdownTimer: number = -1;
  private countdownStart: number = 0;
  private countdownDuration: number = 0;
  private countdownRemaining: number = 1;
  private searchOverlayExpiryTimer: number = -1;
  private readonly searchOverlayExpirySchedule: FaceSearchOverlayExpirySchedule =
    new FaceSearchOverlayExpirySchedule();
  private overlayFramePending: boolean = false;
  private detectionTimingSamples: number = 0;
  private searchTimingSamples: number = 0;
  private overlayTimingSamples: number = 0;
  private previousDetectionAt: number = 0;
  private previousSearchFrameAt: number = 0;
  private previousSearchFrameId: number = -1;
  private searchCameraIdle: boolean = false;
  private readonly overlayFrameCallback: FrameCallback =
    new OverlayFrameCallback((): void => this.drawOverlayFrame());

  /**
   * Restore entry arguments and bind lifecycle; wait for the XComponent Surface before starting the
   * camera.
   *
   * 恢复入口参数并绑定页面生命周期；不在这里启动相机，等待 XComponent Surface 就绪。
   */
  aboutToAppear(): void {
    this.previousKeyboardAvoidMode = this.getUIContext().getKeyboardAvoidMode();
    this.getUIContext().setKeyboardAvoidMode(KeyboardAvoidMode.RESIZE_WITH_CARET);
    this.visible = true;
    this.lastSearchError = undefined;
    this.switchingCamera = false;
    this.cameraReady = false;
    this.flowActive = false;
    this.captureAwaitingConfirmation = false;
    this.pendingReferenceFeature = undefined;
    this.searchOverlayState.clear();
    this.currentMode = this.initialMode;
    this.searchConfiguration = this.initialSearchConfig;
    this.referenceFeature = this.initialReferenceFeature.length > 0 ?
      this.initialReferenceFeature : undefined;
    this.searchSettingsVisible = false;
    this.searchRetryVisible = false;
    this.searchCameraIdle = false;
    this.updateModeGuidance(this.currentMode);
    if (this.isSearchMode()) {
      this.searchScreenPresentation.onIdleStateChanged = (idle: boolean): void =>
        this.setSearchCameraIdle(idle);
    }
    this.realtimeProcessor = this.sdk.createRealtimeProcessor();
    this.lifecycleBinding = this.cameraLifecycle.bind(() => {
      // Stop frames and flows first so late callbacks cannot overwrite restored brightness.
      // 先停帧和流程，避免迟到回调覆盖已恢复的亮度。
      this.suspendCamera();
      this.deactivateCameraPresentation();
    }, () => {
      if (!this.visible) return;
      // On foreground entry, remember the user's current value, apply this camera screen's policy, then
      // resume the camera.
      // 回到前台时重新记录用户当前值，应用当前相机页显示策略后再恢复相机。
      this.activateCameraPresentation();
      if (this.surfaceAvailable) this.onSurfaceReady();
    }, () => {
      if (this.searchSettingsVisible) {
        this.cancelSearchSettings();
        return true;
      }
      return this.resultVisible || this.captureAwaitingConfirmation;
    }, () => {
      this.leaveCameraPage();
    });
    if (this.cameraLifecycle.isActive()) this.activateCameraPresentation();
  }

  /**
   * Invalidate all late callbacks before releasing flows, PixelMaps, the database, and camera.
   *
   * 使所有晚到回调失效，再释放流程、PixelMap、数据库和相机。
   */
  aboutToDisappear(): void {
    this.cameraLifecycle.unbind(this.lifecycleBinding);
    this.leaveCameraPage();
    this.referenceFeature = undefined;
    this.pendingReferenceFeature = undefined;
    this.captureAwaitingConfirmation = false;
    this.releaseCapturedFace();
    this.realtimeProcessor?.close();
    this.realtimeProcessor = undefined;
    this.getUIContext().setKeyboardAvoidMode(this.previousKeyboardAvoidMode);
  }

  private leaveCameraPage(): void {
    this.visible = false;
    this.suspendCamera();
    this.deactivateCameraPresentation();
  }

  private isPageActive(): boolean {
    return this.visible && this.cameraLifecycle.isActive();
  }

  /**
   * Search retains dynamic brightness; other camera screens use 90% on each foreground entry.
   *
   * 搜索页保留动态亮度，其余相机页每次进入前台固定使用 90% 亮度。
   */
  private activateCameraPresentation(): void {
    if (!this.isPageActive()) return;
    const context: common.UIAbilityContext =
      this.getUIContext().getHostContext() as common.UIAbilityContext;
    if (this.isSearchMode()) {
      this.searchScreenPresentation.enter(context);
    } else {
      this.cameraScreenPresentation.enter(context);
    }
  }

  private deactivateCameraPresentation(): void {
    if (this.isSearchMode()) {
      this.searchScreenPresentation.leave();
    } else {
      this.cameraScreenPresentation.leave();
    }
  }

  build() {
    Stack() {
      Column() {
        Stack({ alignContent: Alignment.TopStart }) {
          // Match Android PreviewView: search uses FILL_CENTER, other screens FIT_CENTER. Surface and overlay
          // share the preview center; cropping applies only to search.
          // 对齐 Android PreviewView：搜索使用 FILL_CENTER，其余页面使用 FIT_CENTER。
          // Surface 与 Overlay 都以预览区域中心为原点，裁剪量只在搜索页生效。
          Stack({ alignContent: Alignment.Center }) {
            XComponent({ id: 'hiface-camera-preview', type: XComponentType.SURFACE,
              controller: this.xComponentController })
              .width(this.previewSurfaceWidth()).height(this.previewSurfaceHeight())
              .backgroundColor('#000000')
              .onLoad(() => this.onSurfaceReady())
              .onDestroy(() => this.onSurfaceDestroyed())
          }.width('100%').height('100%')
          Canvas(this.canvasContext).width('100%').height('100%').hitTestBehavior(HitTestMode.None)
            .onReady(() => this.drawOverlay())
          Row() {
            this.CameraIcon($r('app.media.ic_navigation_back'), DemoL10n.text("返回"), (): void => this.onExit())
            Blank()
            if (this.isSearchMode()) {
              this.CameraIcon($r('app.media.ic_search_settings'), DemoL10n.text("搜索设置"), (): void => this.openSearchSettings())
            }
            if (this.currentMode === RealtimeMode.DETECTION || this.isSearchMode() ||
              this.currentMode === RealtimeMode.LIVENESS) {
              this.CameraIcon($r('app.media.ic_camera_switch'), DemoL10n.text("切换摄像头"), (): void => { this.switchCamera(); })
            }
          }.width('100%').padding({
            top: this.isSearchMode() ? 6 : 12,
            left: this.isSearchMode() ? 6 : 12,
            right: this.isSearchMode() ? 6 : 12
          }).zIndex(2)
          if (this.isSearchMode() && this.detectedPeople > 1) {
            Column() {
              Blank()
              Text(DemoL10n.text("检测到 {0} 人 · ", [String(this.detectedPeople)]) +
                (this.currentMode === RealtimeMode.SEARCH_SINGLE ? DemoL10n.text("优先识别最大脸") :
                  DemoL10n.text("本次处理 {0} 人", [String(this.searchedPeople)])))
                .fontSize(11).fontColor('#FFFFFF')
                .padding({ left: 10, right: 10, top: 8, bottom: 8 })
                .backgroundColor('#D9172233').borderRadius(12).maxLines(2)
            }.width('100%').height('100%').padding(12).alignItems(HorizontalAlign.Start)
            .hitTestBehavior(HitTestMode.None).zIndex(1)
          }
          if (this.cameraFailed) {
            Column({ space: 12 }) {
              Text(DemoL10n.text("相机暂时不可用")).fontSize(16).fontColor(this.isGuidedMode() ? DemoTheme.contentText : '#FFFFFF')
              Text(this.cameraError).fontSize(12).fontColor(this.isGuidedMode() ? DemoTheme.contentSecondary : '#FFFFFF')
                .textAlign(TextAlign.Center).padding({ left: 20, right: 20 })
              Button(DemoL10n.text("重试")).backgroundColor(DemoTheme.primary).onClick(() => this.onSurfaceReady())
            }.width('100%').height('100%').justifyContent(FlexAlign.Center).zIndex(1)
          }
        }.layoutWeight(1).width('100%').backgroundColor('#000000').clip(true)
        .onAreaChange((_, area) => {
          this.previewWidth = Number.parseFloat(`${area.width}`);
          this.previewHeight = Number.parseFloat(`${area.height}`);
          this.drawOverlay();
        })
        if (this.isSearchMode()) {
          Column({ space: 10 }) {
            this.SearchStatus()
            Row({ space: 10 }) {
              this.SearchActionButton($r('app.media.ic_search_add'), DemoL10n.text("录入人脸"), true,
                (): void => this.onEnroll())
              this.SearchActionButton($r('app.media.ic_search_library'), DemoL10n.text("人脸库"), false,
                (): void => this.onOpenLibrary())
            }.width('100%')
          }.padding({ left: 16, right: 16, top: 10, bottom: 12 }).width('100%')
        }
      }.width('100%').height('100%').backgroundColor(DemoTheme.searchBackground)
      if (this.captureAwaitingConfirmation) {
        this.EnrollmentConfirmation()
      }
      if (this.resultVisible) {
        this.CompletionDialog()
      }
      if (this.searchSettingsVisible) {
        SearchSettingsDialog({
          viewportHeight: this.dialogViewportHeight,
          initialConfig: this.searchConfig(this.currentMode === RealtimeMode.SEARCH_MULTI ?
            FaceSearchMode.MULTI_FACE : FaceSearchMode.SINGLE_FACE),
          onCancel: (): void => this.cancelSearchSettings(),
          onApply: (config: FaceSearchSessionConfig): void => this.commitSearchSettings(config)
        })
      }
    }.width('100%').height('100%')
      .onAreaChange((_, area) => { this.dialogViewportHeight = Number.parseFloat(`${area.height}`); })
  }

  @Builder
  private CameraIcon(icon: Resource, label: string, action: () => void) {
    Button({ type: ButtonType.Circle }) {
      Stack({ alignContent: Alignment.Center }) {
        Column().width(this.isSearchMode() ? 36 : 48).height(this.isSearchMode() ? 36 : 48)
          .borderRadius(24).backgroundColor(this.isSearchMode() ? '#D9172233' : this.isGuidedMode() || label === DemoL10n.text("切换摄像头") ?
            Color.Transparent : DemoTheme.surfaceOnDark)
        if (this.isSearchMode()) {
          // Search SVGs already have Android-style white outlines; fillColor would incorrectly fill the camera's
          // closed outline.
          // 搜索页 SVG 已使用与 Android 一致的白色描边；fillColor 会把相机闭合外框误填成色块。
          Image(icon).width(20).height(20)
        } else {
          Image(icon).width(24).height(24)
            .fillColor(this.isGuidedMode() || label === DemoL10n.text("切换摄像头") ? DemoTheme.brand : '#FFFFFF')
        }
      }.width(48).height(48)
    }.width(48).height(48).padding(0).backgroundColor(Color.Transparent)
    .accessibilityText(label).enabled(label === DemoL10n.text("返回") ||
      (!this.switchingCamera && (label !== DemoL10n.text("切换摄像头") || this.cameraReady))).onClick(action)
  }

  /**
   * Match Android search buttons: 48 dp height, 20 dp icons, 13 sp medium-weight text, and no default
   * platform content padding.
   *
   * 与 Android 搜索页按钮保持一致：48dp 高、20dp 图标、13sp 中字重，并关闭平台默认内容边距。
   */
  @Builder
  private SearchActionButton(icon: Resource, label: string, primary: boolean, action: () => void) {
    Button({ type: ButtonType.Normal }) {
      Row({ space: 6 }) {
        Image(icon).width(20).height(20)
          .fillColor(primary ? DemoTheme.searchBackground : DemoTheme.textOnDarkSecondary)
        Text(label).fontSize(13).fontWeight(FontWeight.Medium)
          .fontColor(primary ? DemoTheme.searchBackground : '#FFFFFF')
      }.width('100%').height('100%').justifyContent(FlexAlign.Center).alignItems(VerticalAlign.Center)
    }.height(48).layoutWeight(1).padding(0).borderRadius(16)
      .backgroundColor(primary ? DemoTheme.searchAccent : DemoTheme.searchSurface)
      .border({ width: primary ? 0 : 1, color: primary ? Color.Transparent : DemoTheme.searchBorder })
      .onClick(action)
  }

  @Builder
  private SearchStatus() {
    Column({ space: 4 }) {
      Row({ space: 7 }) {
        Column().width(6).height(6).borderRadius(3).backgroundColor(this.statusColor())
        Text(this.searchOptions())
          .fontSize(17).fontWeight(FontWeight.Medium).fontColor('#FFFFFF').maxLines(1)
          .textOverflow({ overflow: TextOverflow.Ellipsis }).layoutWeight(1)
      }.height(28).width('100%')
      Row({ space: 6 }) {
        Text(this.searchMatchCaption.length > 0 ? this.searchMatchCaption :
          this.searchResultText.length > 0 ? this.searchResultText.split('\n')[0] : `${this.guideState} · ${this.resultState}`)
          .fontSize(18).fontWeight(FontWeight.Medium).fontColor('#FFFFFF').maxLines(1)
          .textOverflow({ overflow: TextOverflow.Ellipsis }).layoutWeight(1)
          .onClick(() => {
            AlertDialog.show({ message: `${this.searchOptions()}\n${this.guideState}\n${this.resultState}\n${this.searchResultText}`,
              confirm: { value: DemoL10n.text("返回"), action: (): void => {} } });
          })
        if (this.searchRetryVisible) {
          Button(DemoL10n.text("重试")).height(28).fontSize(12).backgroundColor(Color.Transparent)
            .fontColor(DemoTheme.searchAccent).enabled(this.cameraReady && !this.flowBusy)
            .onClick(() => this.resumeSearch())
        }
      }.height(28).width('100%')
    }.height(76).padding({ left: 12, right: 12, top: 8, bottom: 8 }).width('100%')
    .backgroundColor(DemoTheme.searchSurface).border({ width: 1, color: DemoTheme.searchBorder }).borderRadius(20)
  }

  @Builder
  private EnrollmentConfirmation() {
    Stack() {
      Column().width('100%').height('100%').backgroundColor('#9E000000').onClick(() => {})
      Column() {
        Scroll() {
          Column({ space: 14 }) {
            Text(this.isSearchEnrollment() ? DemoL10n.text("确认录入信息") : DemoL10n.text("确认这张人脸照片？"))
              .fontSize(18).fontWeight(FontWeight.Bold).fontColor(this.dialogText()).width('100%')
            if (this.capturedFace !== undefined) {
              Image(this.capturedFace).width(this.isSearchEnrollment() ? 144 : 188)
                .height(this.isSearchEnrollment() ? 144 : 188).borderRadius(16).objectFit(ImageFit.Cover)
                .scale({ x: this.capturedFaceMirrored ? -1 : 1, y: 1 })
            }
            Text(this.isSearchEnrollment() ? 'Face ID' : DemoL10n.text("确认为清晰无遮挡正脸"))
              .fontSize(14).fontColor(this.dialogText()).width('100%')
              .textAlign(this.isSearchEnrollment() ? TextAlign.Start : TextAlign.Center)
            TextInput({ text: this.cameraEnrollmentFaceId, placeholder: DemoL10n.text("输入 Face ID") })
              .height(48).maxLength(64).fontSize(14).fontColor(this.dialogText()).backgroundColor(this.dialogField())
              .borderRadius(12).caretColor(this.isSearchEnrollment() ? DemoTheme.searchAccent : DemoTheme.primary)
              .enterKeyType(this.isSearchEnrollment() ? EnterKeyType.Next : EnterKeyType.Done)
              .onSubmit(() => {
                if (this.isSearchEnrollment()) this.getUIContext().getFocusController().requestFocus('enrollmentGroup');
                else this.dismissEnrollmentKeyboard();
              })
              .enabled(!this.libraryBusy).onChange((value: string) => { this.cameraEnrollmentFaceId = value; })
            if (this.isSearchEnrollment()) {
              Text(DemoL10n.text("分组选填，留空为未分组。")).fontSize(12).fontColor(DemoTheme.textOnDarkSecondary).width('100%')
              TextInput({ text: this.cameraEnrollmentGroup, placeholder: DemoL10n.text("分组") })
                .id('enrollmentGroup').height(48).fontSize(14).fontColor('#FFFFFF').backgroundColor('#243247')
                .borderRadius(12).caretColor(DemoTheme.searchAccent).enabled(!this.libraryBusy)
                .enterKeyType(EnterKeyType.Done).onSubmit(() => this.dismissEnrollmentKeyboard())
                .onChange((value: string) => { this.cameraEnrollmentGroup = value; })
            }
            if (this.enrollmentError.length > 0) {
              Text(this.enrollmentError).fontSize(12).fontColor(this.isSearchEnrollment() ? DemoTheme.searchError : DemoTheme.contentError)
                .width('100%')
            }
          }.padding({ left: 16, right: 16, top: 20, bottom: 16 }).width('100%')
        }.width('100%').constraintSize({ maxHeight: Math.max(64, this.dialogViewportHeight - 112) })
        .scrollBar(BarState.Auto)
        Row({ space: 10 }) {
          Button(this.isSearchEnrollment() ? DemoL10n.text("重拍") : DemoL10n.text("重试")).layoutWeight(1).height(44).fontSize(16)
            .backgroundColor(this.dialogField()).fontColor(this.dialogText()).enabled(!this.libraryBusy)
            .onClick(() => { this.dismissEnrollmentKeyboard(); this.retryCapture(); })
          Button(this.libraryBusy ? DemoL10n.text("保存中…") : this.isSearchEnrollment() ? DemoL10n.text("保存") : DemoL10n.text("完成"))
            .layoutWeight(1).height(44).fontSize(16).backgroundColor(this.isSearchEnrollment() ? DemoTheme.searchAccent : DemoTheme.primary)
            .fontColor('#FFFFFF').enabled(!this.libraryBusy)
            .onClick(() => { this.dismissEnrollmentKeyboard(); this.confirmEnrollment(); })
        }.width('100%').padding(16)
      }.width(this.isSearchEnrollment() ? 'calc(100% - 34vp)' : '94%').constraintSize({ maxWidth: 560 })
        .backgroundColor(this.isSearchEnrollment() ? DemoTheme.searchSurface : '#FFFFFF')
        .borderRadius(24).clip(true)
    }.width('100%').height('100%')
  }

  private dismissEnrollmentKeyboard(): void {
    this.getUIContext().getFocusController().clearFocus();
  }

  @Builder
  private CompletionDialog() {
    Stack() {
      Column().width('100%').height('100%').backgroundColor('#99000000').onClick(() => {})
      Column({ space: 20 }) {
        Text(this.guideState).fontSize(20).fontColor(DemoTheme.contentText).width('100%')
        Text(this.resultState).fontSize(15).fontColor(DemoTheme.contentSecondary).width('100%')
        Row({ space: 12 }) {
          Button(this.currentMode === RealtimeMode.VERIFICATION ? DemoL10n.text("关闭") : DemoL10n.text("返回"))
            .layoutWeight(1).height(44).fontColor(DemoTheme.brand).backgroundColor(Color.Transparent)
            .onClick(() => this.onExit())
          if (this.resultRetryVisible) {
            Button(this.currentMode === RealtimeMode.VERIFICATION ? DemoL10n.text("再试一次") : DemoL10n.text("重试"))
              .layoutWeight(1).height(44).backgroundColor(DemoTheme.primary)
              .onClick(() => {
                this.resultVisible = false;
                this.resultRetryVisible = false;
                if (this.cameraReady) this.startSelectedFlow();
                else this.onSurfaceReady();
              })
          }
        }.width('100%')
      }.width('86%').padding(24).borderRadius(24).backgroundColor('#FFFFFF')
    }.width('100%').height('100%')
  }

  /**
   * When Surface becomes ready, stop the old session, read host settings, create the controller, and
   * start. cameraSurfaceGeneration isolates old callbacks from navigation, camera switches, and profile
   * fallback.
   *
   * Surface 可用后按“停止旧会话 → 读取宿主设置 → 创建控制器 → 启动”的顺序重建相机。
   * cameraSurfaceGeneration 隔离切页、切摄和规格回退产生的旧回调。
   */
  private async onSurfaceReady(): Promise<void> {
    this.surfaceAvailable = true;
    if (!this.isPageActive()) return;
    const generation: number = ++this.cameraSurfaceGeneration;
    this.cameraReady = false;
    this.cameraFailed = false;
    this.cameraError = '';
    const previous: CameraPreviewController | undefined = this.cameraController;
    this.cameraController = undefined;
    this.cancelFlow();
    this.resetRealtimeState();
    await previous?.stop();
    if (!this.visible || generation !== this.cameraSurfaceGeneration) return;
    this.cameraReady = false;
    let context: common.UIAbilityContext;
    try {
      context = this.getUIContext().getHostContext() as common.UIAbilityContext;
    } catch (error) {
      if (this.visible && generation === this.cameraSurfaceGeneration) {
        this.cameraFailed = true;
        this.cameraError = DemoL10n.text("无法取得页面上下文，请返回后重新进入");
      }
      return;
    }
    let settings: CameraDemoSettings = new CameraDemoSettings();
    try {
      settings = await CameraDemoSettingsStore.load(context);
    } catch (_) {
      if (this.visible && generation === this.cameraSurfaceGeneration) {
        this.resultState = DemoL10n.text("设置读取失败，已恢复默认相机参数");
      }
    }
    if (!this.visible || generation !== this.cameraSurfaceGeneration) {
      return;
    }
    this.mirrorCoordinatesCorrection = settings.mirrorCoordinates;
    const callbacks: CameraPreviewCallbacks = new DemoCameraCallbacks(
      (state: CameraPreviewState, frontCamera: boolean, _profile: string): void => {
        if (!this.visible || generation !== this.cameraSurfaceGeneration) {
          return;
        }
        this.cameraReady = state === CameraPreviewState.RUNNING;
        this.cameraFailed = false;
        this.frontCamera = frontCamera;
        if (this.cameraReady) this.previewAspect = this.cameraController?.getPreviewAspectRatio() ?? 0.75;
        if (this.cameraReady && (this.resultVisible || this.captureAwaitingConfirmation)) {
          this.cameraController?.pauseAnalysis();
        }
        this.drawOverlay();
        if (this.cameraReady && !this.switchingCamera && !this.flowBusy && !this.flowActive &&
          !this.captureAwaitingConfirmation && !this.resultVisible && !this.searchSettingsVisible) {
          this.startSelectedFlow();
        }
      },
      (frame: Yuv420Frame): Promise<void> => {
        if (!this.visible || generation !== this.cameraSurfaceGeneration) return Promise.resolve();
        return this.processFrame(frame);
      },
      (_total: number): void => {},
      (message: string): void => {
        if (this.visible && generation === this.cameraSurfaceGeneration &&
          !this.resultVisible && !this.captureAwaitingConfirmation) {
          this.resultState = message;
        }
      },
      (message: string): void => {
        if (!this.visible || generation !== this.cameraSurfaceGeneration) {
          return;
        }
        this.switchingCamera = false;
        this.cameraReady = false;
        this.cameraFailed = true;
        this.cameraError = message;
        if (!this.resultVisible && !this.captureAwaitingConfirmation) this.resultState = message;
        this.cancelFlow();
        this.resetRealtimeState();
      }
    );
    // Target 15 FPS for active search, 10 FPS idle preview and 5 FPS idle analysis;
    // hardware may fall back.
    // 搜索活跃目标 15 FPS，待机预览 10 FPS、分析 5 FPS；硬件可能回退。
    const sessionFps: number = this.isSearchMode() ?
      SEARCH_ACTIVE_FPS :
      (this.currentMode === RealtimeMode.DETECTION ? 30 : 15);
    this.cameraController = new CameraPreviewController(context, callbacks, settings,
      sessionFps, this.isSearchMode());
    if (this.isSearchMode() && this.searchCameraIdle) {
      this.cameraController.setSessionFrameRate(SEARCH_IDLE_FPS, SEARCH_IDLE_ANALYSIS_FPS);
    }
    const surfaceId: string = this.xComponentController.getXComponentSurfaceId();
    this.cameraController.start(surfaceId, this.frontCamera);
  }

  private onSurfaceDestroyed(): void {
    this.surfaceAvailable = false;
    this.suspendCamera();
  }

  /**
   * Stop frames immediately when hidden or Surface is destroyed; preserve result/confirmation dialogs
   * until resume.
   *
   * 页面隐藏或 Surface 销毁时立即停帧；结果/确认弹窗状态保留到恢复后。
   */
  private suspendCamera(): void {
    this.libraryGeneration += 1;
    if (this.isSearchEnrollment()) this.libraryBusy = false;
    this.enrollmentDatabaseConnection?.close();
    this.enrollmentDatabaseConnection = undefined;
    this.cameraSurfaceGeneration += 1;
    this.cameraReady = false;
    this.cameraFailed = false;
    this.cancelFlow();
    this.resetRealtimeState();
    this.switchingCamera = false;
    if (!this.resultVisible && !this.captureAwaitingConfirmation) this.resultState = DemoL10n.text("返回前台后自动恢复");
    const controller: CameraPreviewController | undefined = this.cameraController;
    this.cameraController = undefined;
    controller?.stop();
  }

  /**
   * Switching cameras during liveness retains the challenge; other modes rebuild requests so old-camera
   * frames cannot advance a new flow.
   *
   * 活体切摄保留当前挑战，其余模式重建业务请求，防止旧镜头帧推进新流程。
   */
  private async switchCamera(): Promise<void> {
    if (this.switchingCamera || !this.cameraReady || this.cameraController === undefined ||
      !this.isPageActive()) return;
    const selectedMode: RealtimeMode = this.currentMode;
    const surfaceGeneration: number = this.cameraSurfaceGeneration;
    const restartFlow: boolean = !this.resultVisible && !this.captureAwaitingConfirmation;
    const preserveRequest: boolean = selectedMode === RealtimeMode.LIVENESS && this.activeRequest !== undefined;
    const preserveSearch: boolean = this.isSearchMode() && this.activeSearchSession !== undefined;
    this.switchingCamera = true;
    this.cameraReady = false;
    if (preserveSearch) {
      // Invalidate old samples on camera switch while reusing the search connection for the new camera.
      // 切镜头作废旧采样，保留搜索连接供新镜头复用。
      this.cameraController.pauseAnalysis();
      this.activeSearchSession?.pause();
      this.flowActive = false;
      this.clearSearchOverlayExpiry();
      this.searchOverlayState.clear();
    } else if (!preserveRequest) this.cancelFlow();
    this.currentMode = selectedMode;
    if (!preserveRequest) {
      this.guideState = this.modeTitle(selectedMode);
      this.resultState = DemoL10n.text("正在切换相机…");
    }
    this.resetRealtimeState();
    try {
      await this.cameraController?.switchCamera();
    } finally {
      if (this.visible && surfaceGeneration === this.cameraSurfaceGeneration) {
        this.switchingCamera = false;
        this.currentMode = selectedMode;
        if (this.resultVisible || this.captureAwaitingConfirmation || (preserveRequest && this.flowActive)) {
          this.drawOverlay();
        } else if (restartFlow && this.cameraReady && !this.resultVisible) {
          if (preserveSearch) this.resumeSearch();
          else this.startSelectedFlow();
        } else {
          this.updateModeGuidance(selectedMode);
        }
      }
    }
  }

  /**
   * One frame-delivery entry point for all real-time modes; drop frames while the processor is busy
   * instead of building a UI-thread queue.
   *
   * 所有实时模式的唯一送帧入口；处理器忙时丢弃当前帧，不在 UI 线程积压任务。
   */
  private async processFrame(frame: Yuv420Frame): Promise<void> {
    const generation: number = this.flowGeneration;
    const surfaceGeneration: number = this.cameraSurfaceGeneration;
    const processor: FaceRealtimeProcessor | undefined = this.realtimeProcessor;
    if (processor === undefined || !this.visible || this.cameraFailed || this.captureAwaitingConfirmation ||
      this.resultVisible || this.searchSettingsVisible ||
      (this.currentMode !== RealtimeMode.DETECTION && !this.flowActive)) {
      return;
    }
    const startedAt: number = DEBUG ? this.monotonicMillis() : 0;
    const task: Promise<HiFaceResult<FaceDetectionResult>> | undefined =
      processor.tryProcessFrame(frame, this.currentMode === RealtimeMode.DETECTION ?
        this.detectionConfig : this.flowDetectionConfig);
    if (task === undefined) {
      return;
    }
    let result: HiFaceResult<FaceDetectionResult>;
    try {
      result = await task;
    } catch (error) {
      if (this.visible && generation === this.flowGeneration &&
        surfaceGeneration === this.cameraSurfaceGeneration) {
        this.resultState = DemoL10n.text("实时推理异常：{0}", [String(error)]);
        this.guideState = DemoL10n.text("识别失败");
        this.resultVisible = this.isGuidedMode();
        this.resultRetryVisible = false;
        this.searchRetryVisible = this.isSearchMode();
        if (this.resultVisible) this.cameraController?.pauseAnalysis();
        this.cancelFlow();
      }
      return;
    }
    if (!this.visible || generation !== this.flowGeneration ||
      surfaceGeneration !== this.cameraSurfaceGeneration) {
      return;
    }
    if (result.isFailure()) {
      const error: HiFaceError = result.requireError();
      if (error.code !== HiFaceErrorCode.NOT_READY) {
        hilog.error(CAMERA_LOG_DOMAIN, CAMERA_LOG_TAG,
          'Frame processing failed: code=%{public}d message=%{public}s', error.code, error.message);
        if (this.currentMode === RealtimeMode.DETECTION) {
          this.cameraFailed = true;
          this.cameraError = DemoErrorUi.describe(error);
        }
        if (this.activeSearchSession === undefined) {
          this.resultState = DemoErrorUi.describe(error);
        }
        if (this.activeRequest !== undefined) {
          this.cancelFlow();
        }
      }
      return;
    }
    const detection: FaceDetectionResult = result.requireValue();
    if (this.currentMode === RealtimeMode.DETECTION) {
      if (DEBUG && this.detectionTimingSamples < 12) {
        const completedAt: number = this.monotonicMillis();
        hilog.info(CAMERA_LOG_DOMAIN, CAMERA_LOG_TAG,
          'Detection timing sdk=%{public}dms result-gap=%{public}dms faces=%{public}d',
          completedAt - startedAt, this.previousDetectionAt === 0 ? 0 :
            completedAt - this.previousDetectionAt, detection.faces.length);
        this.previousDetectionAt = completedAt;
        this.detectionTimingSamples++;
      }
      this.lastDetection = detection;
      this.drawOverlay();
    }
  }

  private updateModeGuidance(mode: RealtimeMode): void {
    this.guideState = this.isGuidedMode() ? DemoL10n.text("平视正对摄像头") : this.modeTitle(mode);
    this.resultState = DemoL10n.text("正在准备相机…");
  }

  /**
   * Start the single business flow selected on the home screen after camera readiness.
   *
   * 相机就绪后按首页选定模式启动唯一业务流程。
   */
  private startSelectedFlow(): void {
    if (!this.isPageActive() || this.flowBusy || this.flowActive) return;
    if (this.isSearchMode() && this.lastSearchError !== undefined &&
      !FaceSearchStatusUi.canResumeSearch(this.lastSearchError)) return;
    if (!this.cameraReady) {
      this.resultState = DemoL10n.text("请等待相机预览与分析链路就绪");
      return;
    }
    if (this.currentMode === RealtimeMode.DETECTION) {
      this.resultState = DemoL10n.text("实时检测已开启");
      return;
    }
    if (this.currentMode === RealtimeMode.CAPTURE || this.isSearchEnrollment()) {
      this.startCapture();
      return;
    }
    if (this.currentMode === RealtimeMode.VERIFICATION) {
      this.startVerification();
      return;
    }
    if (this.currentMode === RealtimeMode.LIVENESS) {
      this.startLiveness();
      return;
    }
    if (this.activeSearchSession !== undefined) {
      this.resumeSearch();
      return;
    }
    this.startSearch(this.currentMode === RealtimeMode.SEARCH_MULTI ?
      FaceSearchMode.MULTI_FACE : FaceSearchMode.SINGLE_FACE);
  }

  private async startCapture(): Promise<void> {
    this.libraryGeneration += 1;
    this.libraryBusy = false;
    this.enrollmentError = '';
    this.resultVisible = false;
    this.resultRetryVisible = false;
    this.cancelFlow();
    this.cameraController?.resumeAnalysis();
    const generation: number = this.flowGeneration;
    this.flowBusy = true;
    this.guideState = DemoL10n.text("准备人脸采集");
    this.resultState = DemoL10n.text("正在加载所需模型…");
    await this.awaitFlowStart(
      () => this.sdk.capture().startCameraCapture(
        this.createFlowListener(this.currentMode, generation)),
      DemoL10n.text("平视正对摄像头"), this.currentMode, generation);
  }

  private async startVerification(): Promise<void> {
    const referenceFeature: string | undefined = this.referenceFeature;
    if (referenceFeature === undefined) {
      this.resultState = DemoL10n.text("请先完成人脸采集");
      this.resultVisible = true;
      this.resultRetryVisible = false;
      this.cameraController?.pauseAnalysis();
      return;
    }
    this.resultVisible = false;
    this.resultRetryVisible = false;
    this.cancelFlow();
    this.cameraController?.resumeAnalysis();
    const generation: number = this.flowGeneration;
    this.flowBusy = true;
    this.guideState = DemoL10n.text("准备 1:1 验证");
    this.resultState = DemoL10n.text("正在准备活体与验证…");
    const actions: ReadonlyArray<FaceAction> | undefined = await this.prepareChallengeActions(
      FaceVerificationFlow.VERIFICATION_ACTION_COUNT, RealtimeMode.VERIFICATION, generation);
    if (actions === undefined) {
      return;
    }
    const config = FaceVerificationFlow.config(referenceFeature, actions);
    await this.awaitFlowStart(
      () => this.sdk.verification().startCameraVerification(config,
        this.createFlowListener(RealtimeMode.VERIFICATION, generation)),
      DemoL10n.text("平视正对摄像头"), RealtimeMode.VERIFICATION, generation);
  }

  private async startLiveness(): Promise<void> {
    this.resultVisible = false;
    this.resultRetryVisible = false;
    this.cancelFlow();
    this.cameraController?.resumeAnalysis();
    const generation: number = this.flowGeneration;
    this.flowBusy = true;
    this.guideState = DemoL10n.text("准备活体检测");
    this.resultState = DemoL10n.text("正在准备活体检测…");
    const actions: ReadonlyArray<FaceAction> | undefined = await this.prepareChallengeActions(
      FaceLivenessFlow.LIVENESS_ACTION_COUNT, RealtimeMode.LIVENESS, generation);
    if (actions === undefined) {
      return;
    }
    const config = FaceLivenessFlow.config(actions);
    await this.awaitFlowStart(
      () => this.sdk.liveness().startCameraLiveness(config,
        this.createFlowListener(RealtimeMode.LIVENESS, generation)),
      DemoL10n.text("平视正对摄像头"), RealtimeMode.LIVENESS, generation);
  }

  /**
   * Prefer resuming the existing session to retain models and the database; the platform controller owns
   * camera readiness.
   *
   * 优先恢复已有会话，保留模型和数据库；相机就绪由平台控制器负责。
   */
  private resumeSearch(): void {
    if (!this.isPageActive() || !this.cameraReady || this.flowBusy) return;
    if (this.lastSearchError !== undefined && !FaceSearchStatusUi.canResumeSearch(this.lastSearchError)) return;
    this.lastSearchError = undefined;
    if (this.activeSearchSession === undefined) {
      this.startSelectedFlow();
      return;
    }
    this.searchRetryVisible = false;
    this.searchOverlayState.clear();
    this.detectedPeople = 0;
    this.searchedPeople = 0;
    this.guideState = DemoL10n.text("正在准备");
    this.resultState = DemoL10n.text("准备完成后自动开始");
    this.activeSearchSession.resume();
    this.flowActive = true;
    this.cameraController?.resumeAnalysis();
    this.drawOverlay();
  }

  private async startSearch(mode: FaceSearchMode): Promise<void> {
    this.cancelFlow();
    const generation: number = this.flowGeneration;
    const realtimeMode: RealtimeMode = mode === FaceSearchMode.SINGLE_FACE ?
      RealtimeMode.SEARCH_SINGLE : RealtimeMode.SEARCH_MULTI;
    this.flowBusy = true;
    this.searchRetryVisible = false;
    this.searchSettingsVisible = false;
    this.guideState = DemoL10n.text("正在准备");
    this.resultState = DemoL10n.text("准备完成后自动开始");
    try {
      const config: FaceSearchSessionConfig = this.searchConfig(mode);
      const started: HiFaceResult<FaceSearchSession> | undefined = await CameraFlowStartQueue.run(
        () => this.sdk.search().startCameraSearch(config, this.createSearchListener(generation)),
        (): boolean => this.visible && generation === this.flowGeneration && this.currentMode === realtimeMode,
        (stale: HiFaceResult<FaceSearchSession>): void => { if (stale.isSuccess()) stale.requireValue().close(); });
      if (started === undefined) return;
      if (!this.visible || generation !== this.flowGeneration || this.currentMode !== realtimeMode) {
        if (started.isSuccess()) started.requireValue().close();
        return;
      }
      this.flowBusy = false;
      if (started.isFailure()) {
        this.onSearchError(started.requireError(), generation);
        return;
      }
      this.activeSearchSession = started.requireValue();
      this.flowActive = true;
      this.lastSearchError = undefined;
      this.cameraController?.resumeAnalysis();
      this.guideState = DemoL10n.text("等待人脸入镜");
      this.resultState = DemoL10n.text("面向镜头，入镜后自动识别");
    } catch (error) {
      if (this.visible && generation === this.flowGeneration && this.currentMode === realtimeMode) {
        this.flowBusy = false;
        this.flowActive = false;
        this.searchRetryVisible = true;
        this.guideState = DemoL10n.text("搜索启动失败");
        this.resultState = DemoL10n.text("启动连续搜索异常：{0}", [String(error)]);
      }
    }
  }

  private createSearchListener(generation: number): FaceSearchListener {
    return new DemoSearchListener(
      (frame: FaceSearchFrame): void => this.onSearchFrame(frame, generation),
      (progress: FaceSearchProgress): void => this.onSearchProgress(progress, generation),
      (result: FaceSearchResult): void => this.onSearchResult(result, generation),
      (error: HiFaceError): void => this.onSearchError(error, generation));
  }

  private onSearchFrame(frame: FaceSearchFrame, generation: number): void {
    if (!this.visible || generation !== this.flowGeneration) return;
    // Progress is deduplicated; synchronize face state every frame so stale state cannot block idle
    // timing.
    // 进度会去重，逐帧同步人脸状态，避免旧状态阻止待机计时。
    this.searchScreenPresentation.updateFacePresence(frame.faces.length > 0);
    const acceptedAt: number = this.monotonicMillis();
    if (DEBUG && this.searchTimingSamples < 24) {
      const scored: boolean = frame.faces.some((face): boolean => face.similarity !== undefined);
      const isNewFrame: boolean = frame.frameId !== this.previousSearchFrameId;
      hilog.info(CAMERA_LOG_DOMAIN, CAMERA_LOG_TAG,
        'Search update frame=%{public}d kind=%{public}s gap=%{public}dms faces=%{public}d',
        frame.frameId, scored ? 'scored' : 'preview',
        isNewFrame && this.previousSearchFrameAt > 0 ? acceptedAt - this.previousSearchFrameAt : 0,
        frame.faces.length);
      if (isNewFrame) {
        this.previousSearchFrameAt = acceptedAt;
        this.previousSearchFrameId = frame.frameId;
      }
      this.searchTimingSamples++;
    }
    const overlayChanged: boolean = this.searchOverlayState.accept(frame, acceptedAt);
    this.scheduleSearchOverlayExpiry();
    if (overlayChanged) this.drawOverlay();
  }

  private onSearchProgress(progress: FaceSearchProgress, generation: number): void {
    if (!this.visible || generation !== this.flowGeneration) return;
    this.searchScreenPresentation.updateFacePresence(progress.detectedCount > 0);
    this.searchRetryVisible = false;
    let overlayChanged: boolean = this.searchOverlayState.setAssociationEnabled(
      this.currentMode === RealtimeMode.SEARCH_MULTI || progress.detectedCount <= 1);
    if (progress.stage === FaceSearchStage.EMPTY_GALLERY) {
      overlayChanged = this.searchOverlayState.rejectScores() || overlayChanged;
    }
    if (this.detectedPeople !== progress.detectedCount) this.detectedPeople = progress.detectedCount;
    if (this.searchedPeople !== progress.selectedCount) this.searchedPeople = progress.selectedCount;
    this.guideState = FaceSearchStatusUi.progressTitle(progress);
    this.resultState = FaceSearchStatusUi.progressDetail(progress);
    this.scheduleSearchOverlayExpiry();
    if (overlayChanged) this.drawOverlay();
  }

  private onSearchResult(result: FaceSearchResult, generation: number): void {
    if (!this.visible || generation !== this.flowGeneration) return;
    this.searchRetryVisible = false;
    const overlayChanged: boolean = this.searchOverlayState.complete(result, this.monotonicMillis());
    this.scheduleSearchOverlayExpiry();
    if (overlayChanged) this.drawOverlay();
  }

  private onSearchError(error: HiFaceError, generation: number): void {
    if (!this.visible || generation !== this.flowGeneration) return;
    this.lastSearchError = error;
    this.cameraController?.pauseAnalysis();
    this.searchScreenPresentation.updateFacePresence(false);
    this.flowBusy = false;
    this.flowActive = false;
    this.searchRetryVisible = FaceSearchStatusUi.canResumeSearch(error);
    this.clearSearchOverlayExpiry();
    this.searchOverlayState.clear();
    this.detectedPeople = 0;
    this.searchedPeople = 0;
    this.guideState = DemoL10n.text("连续搜索已暂停");
    this.resultState = this.searchRetryVisible ? DemoErrorUi.describe(error) :
      DemoL10n.text("搜索恢复失败，请检查配置并重新进入。");
    this.drawOverlay();
  }

  private async prepareChallengeActions(count: number, mode: RealtimeMode,
    generation: number): Promise<ReadonlyArray<FaceAction> | undefined> {
    try {
      const actions: ReadonlyArray<FaceAction> = await FaceChallenge.randomActions(count);
      if (!this.visible || generation !== this.flowGeneration || mode !== this.currentMode) {
        return undefined;
      }
      return actions;
    } catch (error) {
      if (this.visible && generation === this.flowGeneration && mode === this.currentMode) {
        this.flowBusy = false;
        this.flowActive = false;
        this.resultVisible = true;
        this.resultRetryVisible = true;
        this.cameraController?.pauseAnalysis();
        this.guideState = DemoL10n.text("安全挑战生成失败");
        this.resultState = DemoL10n.text("暂时无法准备活体动作，请重试");
      }
      return undefined;
    }
  }

  /**
   * Serialize SDK startup; immediately cancel a successful handle arriving after screen exit.
   *
   * 串行等待 SDK 启动；页面已退出时立即取消晚到的成功句柄。
   */
  private async awaitFlowStart(start: () => Promise<HiFaceResult<FaceRequest>>, readyGuide: string,
    mode: RealtimeMode, generation: number): Promise<void> {
    try {
      const result: HiFaceResult<FaceRequest> | undefined = await CameraFlowStartQueue.run(start,
        (): boolean => this.visible && generation === this.flowGeneration && mode === this.currentMode,
        (stale: HiFaceResult<FaceRequest>): void => { if (stale.isSuccess()) stale.requireValue().cancel(); });
      if (result !== undefined) this.acceptFlowStart(result, readyGuide, mode, generation);
    } catch (error) {
      if (!this.visible || generation !== this.flowGeneration || mode !== this.currentMode) {
        return;
      }
      this.flowBusy = false;
      this.flowActive = false;
      this.resultVisible = true;
      this.resultRetryVisible = false;
      this.cameraController?.pauseAnalysis();
      this.guideState = DemoL10n.text("流程启动失败");
      this.resultState = DemoL10n.text("启动流程异常：{0}", [String(error)]);
      this.drawOverlay();
    }
  }

  private acceptFlowStart(result: HiFaceResult<FaceRequest>, readyGuide: string,
    mode: RealtimeMode, generation: number): void {
    if (!this.visible || generation !== this.flowGeneration || mode !== this.currentMode) {
      if (result.isSuccess()) {
        result.requireValue().cancel();
      }
      return;
    }
    this.flowBusy = false;
    if (result.isFailure()) {
      this.flowActive = false;
      const error: HiFaceError = result.requireError();
      this.resultState = DemoErrorUi.describe(error);
      this.resultRetryVisible = DemoErrorUi.isRetryable(error);
      this.resultVisible = true;
      this.cameraController?.pauseAnalysis();
      return;
    }
    this.activeRequest = result.requireValue();
    this.flowActive = true;
    this.guideState = readyGuide;
    this.resultState = DemoL10n.text("流程已启动");
    this.drawOverlay();
  }

  private createFlowListener(mode: RealtimeMode, generation: number): FaceFlowListener {
    return new DemoFlowListener(
      (progress: FaceFlowProgress): void => this.onFlowProgress(mode, progress, generation),
      (result: FaceFlowResult): void => this.onFlowResult(mode, result, generation),
      (error: HiFaceError): void => this.onFlowError(error, generation));
  }

  private onFlowProgress(mode: RealtimeMode, progress: FaceFlowProgress,
    generation: number): void {
    if (!this.visible || generation !== this.flowGeneration) {
      return;
    }
    this.guideState = FaceFlowUi.progressTitle(progress, this.currentMode === RealtimeMode.CAPTURE || this.isSearchEnrollment());
    this.resultState = FaceFlowUi.progressDetail(mode === RealtimeMode.CAPTURE || mode === RealtimeMode.SEARCH_ENROLLMENT, progress);
    if ((mode === RealtimeMode.CAPTURE || mode === RealtimeMode.SEARCH_ENROLLMENT) &&
      progress.stage === FaceFlowStage.HOLD_STILL) {
      this.startCountdown(CAPTURE_STABLE_COUNTDOWN_MILLIS);
    } else if (progress.stage === FaceFlowStage.FACE_FORWARD ||
      progress.stage === FaceFlowStage.FACE_NOT_FRONTAL || progress.stage === FaceFlowStage.HOLD_STILL ||
      progress.stage === FaceFlowStage.FACE_UNSTABLE) {
      this.resetCountdown();
    } else if (progress.stage === FaceFlowStage.PERFORM_ACTION || progress.action !== undefined) {
      this.startCountdown(ACTION_TIMEOUT_MILLIS * progress.stepCount);
    } else {
      this.resetCountdown();
    }
    this.drawOverlay();
  }

  private onFlowResult(mode: RealtimeMode, result: FaceFlowResult, generation: number): void {
    if (!this.visible || generation !== this.flowGeneration) {
      return;
    }
    if ((mode === RealtimeMode.CAPTURE || mode === RealtimeMode.SEARCH_ENROLLMENT) &&
      result.isPassed() && (result.feature === undefined || result.alignedRgba === undefined)) {
      this.onFlowError(new HiFaceError(HiFaceErrorCode.PROCESSING_FAILED,
        'Guided capture returned an incomplete result'), generation);
      return;
    }
    this.activeRequest = undefined;
    this.flowBusy = false;
    this.flowActive = false;
    this.resultRetryVisible = true;
    this.cameraController?.pauseAnalysis();
    this.resetCountdown();
    this.countdownRemaining = 0;
    if ((mode === RealtimeMode.CAPTURE || mode === RealtimeMode.SEARCH_ENROLLMENT) && result.isPassed() && result.feature !== undefined &&
      result.alignedRgba !== undefined) {
      const retainedCandidate: boolean = this.captureAwaitingConfirmation;
      const captureGeneration: number = ++this.captureGeneration;
      this.flowBusy = true;
      this.guideState = DemoL10n.text("正在准备确认预览");
      this.resultState = DemoL10n.text("正在生成预览，完成后确认录入");
      this.stageEnrollmentCandidate(result.feature, result.alignedRgba, result.alignedSide,
        generation, captureGeneration, retainedCandidate, this.shouldMirrorCoordinates());
    } else if (mode === RealtimeMode.VERIFICATION) {
      this.guideState = result.isPassed() ? DemoL10n.text("1:1 验证通过") : DemoL10n.text("1:1 验证未通过");
      const outcomeMessage: string = `Face ID：${this.referenceFaceId}\n${FaceFlowUi.outcomeTitle(result.outcome)}`;
      // There is no similarity before identity comparison completes; show only Face ID and the outcome
      // reason, not a fabricated zero.
      // 未完成身份比对时没有相似度，只展示 Face ID 和结果原因，不能当作 0 分展示。
      const similarity: number | undefined = result.similarity;
      this.resultState = similarity === undefined
        ? outcomeMessage
        : DemoL10n.text("{0}\n相似度：{1}", [String(outcomeMessage), String(similarity.toFixed(4))]);
      this.resultVisible = true;
    } else if (mode === RealtimeMode.LIVENESS) {
      this.guideState = DemoL10n.text("活体检测");
      const outcomeMessage: string = result.isPassed()
        ? DemoL10n.text("活体检测通过")
        : DemoL10n.text("活体检测未通过：{0}", [String(FaceFlowUi.outcomeTitle(result.outcome))]);
      // Passing or spoof-detected results contain a score; early action timeout or face loss shows only the
      // reason. undefined means passive liveness produced no score, not zero.
      // 通过或检测到非真人时已产生分数；动作超时、丢脸等提前结束时只展示结果原因。
      // undefined 表示未产生静默活体分数，不能当作 0 分展示。
      const livenessScore: number | undefined = result.livenessScore;
      this.resultState = livenessScore === undefined
        ? outcomeMessage
        : DemoL10n.text("{0}\n静默活体分数：{1}", [String(outcomeMessage), String(livenessScore.toFixed(4))]);
      this.resultVisible = true;
    } else {
      this.guideState = FaceFlowUi.outcomeTitle(result.outcome);
      this.resultState = DemoL10n.text("未采集到合格人脸，请重试");
      this.resultVisible = true;
    }
    this.drawOverlay();
  }

  private async confirmEnrollment(): Promise<void> {
    const feature: string | undefined = this.pendingReferenceFeature;
    if (feature === undefined || !this.captureAwaitingConfirmation || this.libraryBusy) return;
    const faceId: string = this.cameraEnrollmentFaceId.trim();
    if (faceId.length === 0) {
      this.enrollmentError = DemoL10n.text("请输入 Face ID");
      return;
    }
    if (this.isSearchEnrollment()) {
      this.enrollReferenceFeature();
      return;
    }
    const captureGeneration: number = this.captureGeneration;
    this.libraryBusy = true;
    this.enrollmentError = DemoL10n.text("正在保存 1:1 基准…");
    try {
      const saved: boolean = await this.onReferenceConfirmed(faceId, feature);
      if (!this.visible || captureGeneration !== this.captureGeneration) return;
      if (!saved) {
        this.enrollmentError = DemoL10n.text("Face ID 与特征安全保存失败，请重试");
      } else if (this.isPageActive()) {
        this.onExit();
      } else {
        // Let confirmed secure writes finish; do not navigate in the background, and show the saved result
        // after resume.
        // 已确认的安全写入自然完成；后台不触发导航，恢复后展示保存结果。
        this.captureAwaitingConfirmation = false;
        this.pendingReferenceFeature = undefined;
        this.resultVisible = true;
        this.resultRetryVisible = false;
        this.guideState = DemoL10n.text("人脸已保存");
        this.resultState = `Face ID：${faceId}`;
        await this.releaseCapturedFace();
      }
    } catch (_) {
      if (this.visible && captureGeneration === this.captureGeneration) {
        this.enrollmentError = DemoL10n.text("Face ID 与特征安全保存失败，请重试");
      }
    } finally {
      if (this.visible && captureGeneration === this.captureGeneration) this.libraryBusy = false;
    }
  }

  private async retryCapture(): Promise<void> {
    if (this.libraryBusy) return;
    this.captureAwaitingConfirmation = false;
    this.pendingReferenceFeature = undefined;
    this.enrollmentError = '';
    this.resultVisible = false;
    const generation: number = this.flowGeneration;
    await this.releaseCapturedFace();
    if (!this.visible || generation !== this.flowGeneration) return;
    if (this.cameraReady) this.startSelectedFlow();
    else if (this.surfaceAvailable) this.onSurfaceReady();
  }

  private async enrollReferenceFeature(): Promise<void> {
    const feature: string | undefined = this.pendingReferenceFeature;
    const faceId: string = this.cameraEnrollmentFaceId.trim();
    if (feature === undefined || faceId.length === 0 || this.libraryBusy) return;
    const tag: string = '';
    const group: string = this.cameraEnrollmentGroup.trim();
    const generation: number = ++this.libraryGeneration;
    this.libraryBusy = true;
    this.enrollmentError = DemoL10n.text("正在检查搜索库中的 faceId…");
    try {
      const database: FaceSearchDatabase | undefined = await this.enrollmentDatabase(generation);
      if (database === undefined || !this.isCurrentLibraryTask(generation)) return;
      const existing = await database.get(faceId);
      if (!this.isCurrentLibraryTask(generation)) return;
      if (existing.isSuccess()) {
        this.libraryBusy = false;
        this.confirmCameraEnrollmentOverwrite(database, faceId, feature, existing.requireValue().tag, group, generation);
        return;
      }
      if (existing.requireError().code !== HiFaceErrorCode.NOT_FOUND) {
        this.enrollmentError = DemoL10n.text("检查失败：{0}", [String(DemoErrorUi.describe(existing.requireError()))]);
        return;
      }
      this.libraryBusy = false;
      await this.persistCameraEnrollment(database, faceId, feature, tag, group, false, generation);
    } catch (error) {
      if (this.isCurrentLibraryTask(generation)) this.enrollmentError = DemoL10n.text("保存失败：{0}", [String(error)]);
    } finally {
      if (this.isCurrentLibraryTask(generation)) this.libraryBusy = false;
    }
  }

  private confirmCameraEnrollmentOverwrite(database: FaceSearchDatabase, faceId: string,
    feature: string, tag: string, group: string, generation: number): void {
    this.getUIContext().showAlertDialog({
      title: DemoL10n.text("覆盖搜索库记录？"),
      message: DemoL10n.text("Face ID “{0}” 已存在，替换为本次采集？", [String(faceId)]),
      primaryButton: {
        value: DemoL10n.text("覆盖"),
        fontColor: DemoTheme.contentError,
        action: (): void => {
          this.persistCameraEnrollment(database, faceId, feature, tag, group, true, generation);
        }
      },
      secondaryButton: {
        value: DemoL10n.text("取消"),
        action: (): void => {
          if (this.isCurrentLibraryTask(generation)) this.enrollmentError = DemoL10n.text("已取消覆盖");
        }
      }
    });
  }

  private async persistCameraEnrollment(database: FaceSearchDatabase, faceId: string,
    feature: string, tag: string, group: string, overwrite: boolean,
    generation: number): Promise<void> {
    if (!this.isCurrentLibraryTask(generation) || this.libraryBusy) return;
    this.libraryBusy = true;
    this.enrollmentError = overwrite ? DemoL10n.text("正在覆盖搜索库记录…") : DemoL10n.text("正在新增搜索库记录…");
    try {
      const saved: HiFaceResult<boolean> =
        await database.saveFeature(faceId, feature, Date.now(), tag, group);
      if (!this.isCurrentLibraryTask(generation)) return;
      if (saved.isSuccess()) {
        try { this.getUIContext().getPromptAction().showToast({ message: DemoL10n.text("已录入 {0}", [String(faceId)]) }); } catch (_) {}
        this.libraryBusy = false;
        this.cameraEnrollmentFaceId = '';
        this.cameraEnrollmentGroup = '';
        this.captureAwaitingConfirmation = false;
        this.pendingReferenceFeature = undefined;
        await this.releaseCapturedFace();
        if (this.isCurrentLibraryTask(generation)) this.onExit();
      } else {
        this.enrollmentError = DemoL10n.text("保存失败：{0}", [String(DemoErrorUi.describe(saved.requireError()))]);
      }
    } catch (error) {
      if (this.isCurrentLibraryTask(generation)) this.enrollmentError = DemoL10n.text("保存失败：{0}", [String(error)]);
    } finally {
      if (this.isCurrentLibraryTask(generation)) this.libraryBusy = false;
    }
  }

  private async enrollmentDatabase(generation: number): Promise<FaceSearchDatabase | undefined> {
    if (this.enrollmentDatabaseConnection !== undefined) return this.enrollmentDatabaseConnection;
    try {
      const context: common.UIAbilityContext =
        this.getUIContext().getHostContext() as common.UIAbilityContext;
      const opened: HiFaceResult<FaceSearchDatabase> = await HiFaceSdk.openSearchDatabase(context);
      if (!this.isCurrentLibraryTask(generation)) {
        if (opened.isSuccess()) opened.requireValue().close();
        return undefined;
      }
      if (opened.isFailure()) {
        this.enrollmentError = DemoL10n.text("打开失败：{0}", [String(DemoErrorUi.describe(opened.requireError()))]);
        return undefined;
      }
      this.enrollmentDatabaseConnection = opened.requireValue();
      return this.enrollmentDatabaseConnection;
    } catch (error) {
      if (this.isCurrentLibraryTask(generation)) this.enrollmentError = DemoL10n.text("打开失败：{0}", [String(error)]);
      return undefined;
    }
  }

  private isCurrentLibraryTask(generation: number): boolean {
    return this.isPageActive() &&
      generation === this.libraryGeneration;
  }

  private onFlowError(error: HiFaceError, generation: number): void {
    if (!this.visible || generation !== this.flowGeneration) {
      return;
    }
    this.activeRequest = undefined;
    this.flowBusy = false;
    this.flowActive = false;
    this.resetCountdown();
    if (this.currentMode === RealtimeMode.CAPTURE &&
      DemoErrorUi.shouldRestartCapture(error) && this.cameraReady) {
      try { this.getUIContext().getPromptAction().showToast({ message: DemoErrorUi.describe(error) }); } catch (_) {}
      this.guideState = DemoL10n.text("正在恢复人脸采集");
      this.resultState = DemoL10n.text("偶发处理失败，已自动开始下一次采集");
      this.resultVisible = false;
      this.resultRetryVisible = false;
      this.startCapture();
      return;
    }
    this.resultVisible = true;
    this.resultRetryVisible = DemoErrorUi.isRetryable(error);
    this.cameraController?.pauseAnalysis();
    this.guideState = DemoL10n.text("流程执行失败");
    this.resultState = DemoErrorUi.describe(error);
    this.drawOverlay();
  }

  /**
   * Finish the flow idempotently and invalidate in-flight callbacks.
   *
   * 幂等结束流程，并作废在途回调。
   */
  private cancelFlow(): void {
    this.resetCountdown();
    this.clearSearchOverlayExpiry();
    this.flowGeneration += 1;
    this.activeRequest?.cancel();
    this.activeRequest = undefined;
    this.activeSearchSession?.close();
    this.activeSearchSession = undefined;
    this.searchPausedForSettings = false;
    this.flowBusy = false;
    this.flowActive = false;
    this.searchOverlayState.clear();
  }

  /**
   * Create a confirmation preview; a failed new capture may retain the previous pending sample, with
   * explicit PixelMap ownership and cleanup.
   *
   * 生成确认预览；新采集失败时可保留上一份待确认样本，PixelMap 所有权始终显式释放。
   */
  private async stageEnrollmentCandidate(feature: string, rgba: ArrayBuffer, side: number,
    flowGeneration: number, captureGeneration: number, retainedCandidate: boolean,
    mirrored: boolean): Promise<void> {
    const pixelMap: image.PixelMap | undefined = await PixelMapResource.createRgba(
      rgba, side, side);
    if (pixelMap === undefined) {
      if (this.visible && flowGeneration === this.flowGeneration &&
        captureGeneration === this.captureGeneration) {
        this.flowBusy = false;
        this.resultVisible = !retainedCandidate;
        this.guideState = DemoL10n.text("录入预览生成失败");
        this.resultState = retainedCandidate ? DemoL10n.text("已保留原样本，可确认或重试") :
          DemoL10n.text("未保存本次样本，请重新录入");
      }
      return;
    }
    if (!this.visible || flowGeneration !== this.flowGeneration ||
      captureGeneration !== this.captureGeneration) {
      await PixelMapResource.release(pixelMap);
      return;
    }
    const previous: image.PixelMap | undefined = this.capturedFace;
    this.capturedFace = pixelMap;
    this.capturedFaceMirrored = mirrored;
    this.pendingReferenceFeature = feature;
    this.captureAwaitingConfirmation = true;
    this.flowBusy = false;
    this.guideState = DemoL10n.text("请确认录入样本");
    this.resultState = DemoL10n.text("预览 {0}×{1}；确认后替换基准", [String(side), String(side)]);
    await PixelMapResource.release(previous);
  }

  private async releaseCapturedFace(): Promise<void> {
    this.captureGeneration += 1;
    const pixelMap: image.PixelMap | undefined = this.capturedFace;
    this.capturedFace = undefined;
    await PixelMapResource.release(pixelMap);
  }

  private resetRealtimeState(): void {
    this.lastDetection = undefined;
    this.detectedPeople = 0;
    if (this.isSearchMode()) this.searchScreenPresentation.updateFacePresence(false);
    this.searchedPeople = 0;
    this.searchTimingSamples = 0;
    this.previousSearchFrameAt = 0;
    this.previousSearchFrameId = -1;
    this.realtimeProcessor?.resetStatistics();
    this.drawOverlay();
  }

  private modeTitle(mode: RealtimeMode): string {
    if (mode === RealtimeMode.CAPTURE || mode === RealtimeMode.SEARCH_ENROLLMENT) {
      return DemoL10n.text("人脸采集");
    }
    if (mode === RealtimeMode.VERIFICATION) {
      return DemoL10n.text("1:1 人脸验证");
    }
    if (mode === RealtimeMode.LIVENESS) {
      return DemoL10n.text("活体检测");
    }
    if (mode === RealtimeMode.SEARCH_SINGLE) {
      return DemoL10n.text("1:N 人脸搜索");
    }
    if (mode === RealtimeMode.SEARCH_MULTI) {
      return DemoL10n.text("M:N 人脸搜索");
    }
    return DemoL10n.text("实时检测");
  }

  private isSearchMode(): boolean {
    return this.currentMode === RealtimeMode.SEARCH_SINGLE ||
      this.currentMode === RealtimeMode.SEARCH_MULTI;
  }

  private setSearchCameraIdle(idle: boolean): void {
    if (!this.isSearchMode() || this.searchCameraIdle === idle) return;
    this.searchCameraIdle = idle;
    this.cameraController?.setSessionFrameRate(
      idle ? SEARCH_IDLE_FPS : SEARCH_ACTIVE_FPS, idle ? SEARCH_IDLE_ANALYSIS_FPS : SEARCH_ACTIVE_FPS);
  }

  private searchConfig(mode: FaceSearchMode): FaceSearchSessionConfig {
    const config: FaceSearchSessionConfig = this.searchConfiguration;
    if (config.mode === mode) return config;
    // Reuse other settings when changing modes; clamp the C ABI float upper boundary into the
    // constructor's valid input range.
    // 切模式时复用其余配置；C ABI float 的上边界需夹回构造器的有效输入范围。
    return new FaceSearchSessionConfig(mode, config.group, config.tag,
      mode === FaceSearchMode.SINGLE_FACE && config.needsLiveness,
      Math.min(0.85, Math.max(0.75, config.threshold)), config.searchIntervalMillis,
      config.searchTimeoutMillis, config.maxFacesPerFrame);
  }

  private openSearchSettings(): void {
    if (this.searchSettingsVisible || this.flowBusy) return;
    const session: FaceSearchSession | undefined = this.activeSearchSession;
    if (session !== undefined) {
      session.pause();
      this.searchPausedForSettings = true;
    }
    this.searchSettingsVisible = true;
  }

  private cancelSearchSettings(): void {
    this.searchSettingsVisible = false;
    if (this.searchPausedForSettings && this.activeSearchSession !== undefined) {
      this.resumeSearch();
      this.searchPausedForSettings = false;
    } else if (this.cameraReady && !this.flowBusy && !this.flowActive) {
      // Backgrounding closes the old session, so returning and cancelling a draft must also resume search.
      // 后台会关闭旧会话，返回后取消草稿也需恢复搜索。
      this.startSelectedFlow();
    }
  }

  private commitSearchSettings(config: FaceSearchSessionConfig): void {
    const mode: RealtimeMode = config.mode === FaceSearchMode.SINGLE_FACE ?
      RealtimeMode.SEARCH_SINGLE : RealtimeMode.SEARCH_MULTI;
    const modeChanged: boolean = this.currentMode !== mode;
    if (modeChanged) {
      this.cancelFlow();
      this.currentMode = mode;
    }
    this.searchConfiguration = config;
    this.onSearchConfigChanged(config);
    this.searchSettingsVisible = false;
    this.searchRetryVisible = false;
    if (modeChanged) {
      if (this.lastSearchError !== undefined && !FaceSearchStatusUi.canResumeSearch(this.lastSearchError)) {
        this.onSearchError(this.lastSearchError, this.flowGeneration);
      } else {
        this.updateModeGuidance(this.currentMode);
        this.drawOverlay();
        if (this.cameraReady) this.startSelectedFlow();
      }
      return;
    }
    this.applySearchSettings();
    this.searchPausedForSettings = false;
  }

  private applySearchSettings(): void {
    const session: FaceSearchSession | undefined = this.activeSearchSession;
    if (session === undefined) {
      if (this.lastSearchError !== undefined && !FaceSearchStatusUi.canResumeSearch(this.lastSearchError)) {
        this.onSearchError(this.lastSearchError, this.flowGeneration);
      } else if (this.cameraReady) this.resumeSearch();
      return;
    }
    try {
      const mode: FaceSearchMode = this.currentMode === RealtimeMode.SEARCH_MULTI ?
        FaceSearchMode.MULTI_FACE : FaceSearchMode.SINGLE_FACE;
      session.setConfig(this.searchConfig(mode));
      // While paused, setConfig only stores configuration; explicitly resume a valid session.
      // 暂停时 setConfig 只保存配置，需要显式恢复有效会话。
      if (this.lastSearchError !== undefined && !FaceSearchStatusUi.canResumeSearch(this.lastSearchError)) {
        this.onSearchError(this.lastSearchError, this.flowGeneration);
        return;
      }
      this.resumeSearch();
    } catch (error) {
      this.searchRetryVisible = true;
      this.guideState = DemoL10n.text("搜索设置应用失败");
      this.resultState = DemoL10n.text("请重试搜索");
    }
  }

  private statusColor(): string {
    return this.searchRetryVisible || this.lastSearchError !== undefined ? DemoTheme.searchError : DemoTheme.searchAccent;
  }

  private isGuidedMode(): boolean {
    return this.currentMode === RealtimeMode.CAPTURE || this.isSearchEnrollment() ||
      this.currentMode === RealtimeMode.VERIFICATION || this.currentMode === RealtimeMode.LIVENESS;
  }

  private isSearchEnrollment(): boolean {
    return this.currentMode === RealtimeMode.SEARCH_ENROLLMENT;
  }

  private dialogText(): string { return this.isSearchEnrollment() ? '#FFFFFF' : DemoTheme.brand; }
  private dialogField(): string { return this.isSearchEnrollment() ? '#243247' : '#F1EEF3'; }

  private previewSurfaceWidth(): number | string {
    if (this.previewWidth <= 0 || this.previewHeight <= 0) return '100%';
    return this.previewBounds().width;
  }

  private previewSurfaceHeight(): number | string {
    if (this.previewWidth <= 0 || this.previewHeight <= 0) return '100%';
    return this.previewBounds().height;
  }

  private previewBounds(): CameraPreviewBounds {
    return CameraPreviewLayout.centeredBounds(this.currentMode, this.previewAspect,
      this.previewWidth, this.previewHeight);
  }

  private searchOptions(): string {
    return this.currentMode === RealtimeMode.SEARCH_MULTI ? DemoL10n.text("M:N · 不检测姿态与活体") :
      DemoL10n.text("1:N · 活体{0}", [String(this.searchConfiguration.needsLiveness ? DemoL10n.text("已开启") : DemoL10n.text("未开启"))]);
  }

  private startCountdown(duration: number): void {
    if (this.countdownStart > 0 || duration <= 0) return;
    this.countdownStart = this.monotonicMillis();
    this.countdownDuration = duration;
    this.countdownTimer = setInterval(() => {
      this.countdownRemaining = Math.max(0, 1 -
        (this.monotonicMillis() - this.countdownStart) / this.countdownDuration);
      this.drawOverlay();
      if (this.countdownRemaining <= 0) {
        clearInterval(this.countdownTimer);
        this.countdownTimer = -1;
      }
    }, 33);
  }

  private resetCountdown(): void {
    if (this.countdownTimer >= 0) clearInterval(this.countdownTimer);
    this.countdownTimer = -1;
    this.countdownStart = 0;
    this.countdownRemaining = 1;
  }

  private scheduleSearchOverlayExpiry(): void {
    const deadline: number = this.visible && this.isSearchMode() ?
      this.searchOverlayState.expiresAt() : 0;
    if (!this.searchOverlayExpirySchedule.replace(deadline)) return;
    if (this.searchOverlayExpiryTimer >= 0) clearTimeout(this.searchOverlayExpiryTimer);
    this.searchOverlayExpiryTimer = -1;
    if (deadline <= 0) return;
    const flowGeneration: number = this.flowGeneration;
    const surfaceGeneration: number = this.cameraSurfaceGeneration;
    this.searchOverlayExpiryTimer = setTimeout(() => {
      if (!this.visible || !this.isSearchMode() || flowGeneration !== this.flowGeneration ||
        surfaceGeneration !== this.cameraSurfaceGeneration) return;
      this.searchOverlayExpiryTimer = -1;
      this.searchOverlayExpirySchedule.clear();
      if (this.searchOverlayState.expire(this.monotonicMillis())) this.drawOverlay();
      this.scheduleSearchOverlayExpiry();
    }, Math.max(1, deadline - this.monotonicMillis()));
  }

  private clearSearchOverlayExpiry(): void {
    if (this.searchOverlayExpiryTimer >= 0) clearTimeout(this.searchOverlayExpiryTimer);
    this.searchOverlayExpiryTimer = -1;
    this.searchOverlayExpirySchedule.clear();
  }

  /**
   * Coalesce drawing requests within one UI frame instead of immediately redrawing Canvas for each
   * detection/search callback.
   *
   * 合并同一 UI 帧内的多次绘制请求，避免每个检测/搜索回调都立即重绘 Canvas。
   */
  private drawOverlay(): void {
    const resultText: string = this.isSearchMode() ? this.searchOverlayState.resultText() : '';
    if (this.searchResultText !== resultText) this.searchResultText = resultText;
    const caption: string = this.isSearchMode() ? this.searchOverlayState.bestMatchCaption(
      this.currentMode !== RealtimeMode.SEARCH_MULTI && this.searchConfiguration.needsLiveness) : '';
    if (this.searchMatchCaption !== caption) this.searchMatchCaption = caption;
    if (this.overlayFramePending) return;
    this.overlayFramePending = true;
    try {
      this.getUIContext().postFrameCallback(this.overlayFrameCallback);
    } catch (_) {
      this.overlayFramePending = false;
      this.drawOverlayFrame();
    }
  }

  private drawOverlayFrame(): void {
    this.overlayFramePending = false;
    if (!this.visible) return;
    const width: number = this.previewWidth;
    const height: number = this.previewHeight;
    const context: CanvasRenderingContext2D = this.canvasContext;
    if (width <= 0 || height <= 0) return;
    context.clearRect(0, 0, width, height);
    if (this.isGuidedMode()) {
      FaceCaptureOverlay.draw(context, width, height, this.guideState, this.countdownRemaining);
      return;
    }
    if (this.currentMode === RealtimeMode.DETECTION) {
      const detection: FaceDetectionResult | undefined = this.lastDetection;
      if (detection === undefined || detection.imageWidth <= 0 || detection.imageHeight <= 0) return;
      const drawStartedAt: number = DEBUG ? this.monotonicMillis() : 0;
      FaceDetectionOverlay.draw(context, detection, width, height, this.shouldMirrorCoordinates());
      if (DEBUG && this.overlayTimingSamples < 12) {
        hilog.info(CAMERA_LOG_DOMAIN, CAMERA_LOG_TAG,
          'Overlay command timing draw=%{public}dms faces=%{public}d',
          this.monotonicMillis() - drawStartedAt, detection.faces.length);
        this.overlayTimingSamples++;
      }
    } else if (this.isSearchMode()) {
      FaceSearchOverlay.draw(context, this.searchOverlayState, width, height,
        this.shouldMirrorCoordinates(), this.monotonicMillis());
    }
  }

  private shouldMirrorCoordinates(): boolean {
    return this.frontCamera !== this.mirrorCoordinatesCorrection;
  }

  private monotonicMillis(): number {
    return systemDateTime.getUptime(systemDateTime.TimeType.STARTUP);
  }
}
