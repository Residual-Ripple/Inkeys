module;

#include "../../../IdtMain.h"

#include "../../../IdtConfiguration.h"
#include <dwrite_1.h>
#include "../../../IdtDraw.h"
#include "../../Business/LegacyDrawState.hpp"
#include "../../../IdtState.h"
#include "../../Window/Window.Legacy.hpp"
#include "Bar.A2.h"
#include "Bar.BottomDock.h"
#include "Bar.MainLayoutPolicy.h"
#include "Bar.PresentDecision.h"
#include "Bar.PresentationProbe.h"
#include "../../../resource.h"
#include <algorithm>
#include <cmath>
#include <shellapi.h>
#include <bit>
#include <limits>

#pragma comment(lib, "shell32.lib")

#pragma comment(lib, "dxguid.lib")

module Inkeys.UI.Bar;
import :Main;
import :Rendering;
import :Layout;
import :Atomic;
import :Zoom;
import :Theme;

import Inkeys.UI.Bar.FramePacing;
import Inkeys.UI.RenderPipeline;
import Inkeys.Display;
import Inkeys.Window;
import Inkeys.UI.StartupPreview;
import Inkeys.UI.Setting;
import Inkeys.Other.Config;

import <ranges>;

import Inkeys.Conv.Color;
import Inkeys.Other.Inputs;
import Inkeys.Conv.Text;

// Interaction 实现单元独占窗口消息状态；协调器仅通过窄接口读取或投递。
bool ReadColorPickerEntryPressed();
void RequestBarBorderCursorSuspend();

namespace
{
	IdtAtomic<bool> currentPageHasContent = false;
	std::atomic_bool contentStateUpdatesReady = false;
	std::atomic_bool pptPresentationActive = false;
	// 几何只使用渲染线程已接管的场景；语义发布不能提前改变在途帧的 dock 线。
	std::atomic_bool pptDockSceneActive = false;
	std::atomic<HWND> pptPresentationWindow = nullptr;
	std::atomic<HMONITOR> pptSceneMonitor = nullptr;
	std::mutex pptSceneMutex;
	Inkeys::UI::Bar::BarPptSceneState pptSceneState;
	std::mutex pptBusinessFocusMutex;
	std::function<void()> pptBusinessFocusCallback;
	Inkeys::UI::Bar::BarA2CallbackDispatcher endShowDispatcher;
	std::atomic_uint64_t autoCollapseSceneRevision{ 1 };
	std::atomic_bool whiteboardActive = false;
	std::atomic<WorkspaceScene> committedChromeScene = WorkspaceScene::Desktop;
	std::atomic_bool whiteboardBottomDockRequested = false;
	std::atomic_bool whiteboardDockLockActive = false;

	// 「仅放映时显示」与整体不透明度：只在设置/场次边沿改变，渲染每帧只读原子量。
	constexpr UINT BarTrayIconId = 1;
	constexpr UINT BarTrayMenuOpenSettings = 0x494B5001;
	constexpr UINT BarTrayMenuOnlyPresenting = 0x494B5002;
	constexpr UINT BarTrayMenuExit = 0x494B5003;

	std::atomic_bool presentationVisibilityHidden = false;
	std::atomic_bool trayReady = false;

	[[nodiscard]] double ClampedBarOpacityPercent() noexcept
	{
		const int percent = Inkeys::config.UI.Bar.OpacityPercent.load();
		return static_cast<double>(std::clamp(percent, 10, 100));
	}

}
extern constexpr double BarButtonHoverFadeDur = BarButtonHoverFadeDurationSeconds;
// Rendering 与 topology 共享同一组 module-linkage 常量，拆分后不复制数值。
extern constexpr double BarDrawAttributeExpandedHeight = 185.0;
extern constexpr double BarDrawAttributeCompactWidth = 60.0;
extern constexpr double BarDrawAttributeCompactScale =
	BarDrawAttributeCompactWidth / BarDrawAttributeExpandedWidth;
extern constexpr double BarDrawAttributeCompactHeight =
	BarDrawAttributeExpandedHeight * BarDrawAttributeCompactScale;
extern constexpr double BarDrawAttributeThicknessHeight = 105.0;
extern constexpr double BarDrawAttributeThicknessControlHeight = 30.0;
extern constexpr double BarDrawAttributeSurfaceOpacity = 0.95;
extern constexpr double BarDrawAttributeThicknessContentInset =
	BarDrawAttributeGap * 2.0;
// 几何属性与绘制属性共用同一组分割线参数，保证光影和圆角一致。
extern constexpr double BarUiDividerRadius = 0.5;
extern constexpr double BarUiDividerCursorLightIntensity = 0.30;
extern constexpr double BarDrawAttributePenTypeButtonWidth = 115.0;
extern constexpr double BarDrawAttributePenTypeButtonHeight = 30.0;
extern constexpr double BarDrawAttributePenTypeLeft =
	BarDrawAttributeExpandedWidth - BarDrawAttributeGap
	- BarDrawAttributePenTypeButtonWidth;
extern constexpr double BarDrawAttributeThicknessDividerLeft =
	BarDrawAttributeGap;
extern constexpr double BarDrawAttributeThicknessDividerRight =
	BarDrawAttributePenTypeLeft - BarDrawAttributeGap;
extern constexpr double BarDrawAttributeThicknessDividerWidth =
	BarDrawAttributeThicknessDividerRight
	- BarDrawAttributeThicknessDividerLeft;
extern constexpr double BarDrawAttributeThicknessAdjustX =
	BarDrawAttributeThicknessDividerRight
	- BarDrawAttributeThicknessControlHeight;
extern constexpr double BarDrawAttributeThicknessPresetStartX =
	BarDrawAttributeThicknessAdjustX
	- (BarDrawAttributeThicknessControlHeight + BarDrawAttributeGap) * 3.0;
extern constexpr double BarDrawAttributePenTypeExtensionDividerX = 85.0;
extern constexpr double BarDrawAttributePenTypeExtensionWidth =
	BarDrawAttributePenTypeButtonWidth
	- BarDrawAttributePenTypeExtensionDividerX;
extern constexpr double BarDrawAttributePenTypeMenuRowHeight = 30.0;
extern constexpr double BarDrawAttributePenTypeMenuPadding = 5.0;
extern constexpr double BarDrawAttributePenTypeMenuCheckAreaWidth = 26.0;
extern constexpr double BarDrawAttributePenTypeMenuCheckSize = 12.0;
extern constexpr double BarDrawAttributePenTypeMenuCheckInset =
	(BarDrawAttributePenTypeMenuCheckAreaWidth
		- BarDrawAttributePenTypeMenuCheckSize) / 2.0;
extern constexpr double BarDrawAttributePenTypeMenuHeight =
	BarDrawAttributePenTypeMenuPadding * 2.0
	+ BarDrawAttributePenTypeMenuRowHeight * 2.0;
extern constexpr double BarGeometryAttributeExpandedWidth = 335.0;
extern constexpr double BarGeometryAttributeExpandedHeight = 100.0;
extern constexpr double BarGeometryAttributeCompactWidth = 60.0;
extern constexpr double BarGeometryAttributeCompactScale =
	BarGeometryAttributeCompactWidth / BarGeometryAttributeExpandedWidth;
extern constexpr double BarGeometryAttributeCompactHeight =
	BarGeometryAttributeExpandedHeight * BarGeometryAttributeCompactScale;
extern constexpr double BarGeometryAttributeGap = 5.0;
extern constexpr double BarGeometryAttributeThicknessButtonSize = 30.0;
extern constexpr double BarGeometryAttributeDividerCursorLightIntensity =
	BarUiDividerCursorLightIntensity;
extern constexpr double BarThicknessSliderTrackHeight = 4.0;
extern constexpr double BarThicknessSliderThumbCenterDiameter = 12.0;
extern constexpr double BarThicknessSliderThumbHoverCenterDiameter = 14.0;
extern constexpr double BarThicknessSliderThumbPressedCenterDiameter = 10.0;
extern constexpr double BarThicknessSliderThumbAnimationDur = 0.28;
	extern constexpr double BarThicknessSliderPressAnimationDur = 0.12;
	extern constexpr double BarThicknessPreviewPopupAnimationDur = 0.40;
	extern constexpr double BarThicknessPreviewNumberAnimationDur = 0.18;
	extern constexpr double BarThicknessPreviewPopupPadding = 8.0;
	extern constexpr double BarThicknessPreviewPopupThumbGap = 10.0;
	extern constexpr double BarThicknessPreviewAvoidGap = 5.0;
	extern constexpr double BarThicknessPreviewNumberGap = 5.0;
	extern constexpr double BarThicknessPreviewNumberInset = 5.0;
	extern constexpr double BarThicknessPreviewNumberFontSize = 13.0;
	// FineDial 激活区沿 Preview 展开方向排列，数值均为未缩放 DIP。
	extern constexpr double BarThicknessFineDialActivationPreviewBaseOpacity = 0.5;
	extern constexpr double BarThicknessFineDialActivationPreviewEnterDur = 0.18;
	extern constexpr double BarThicknessFineDialActivationPreviewFadeOutDur = 0.30;
	extern constexpr double BarThicknessFineDialSelectionTransitionDur = 0.18;
	extern constexpr double BarThicknessFineDialPopupPanelGapDip = 8.0;
	extern constexpr double BarThicknessFineDialTransitionDur = 0.28;
	extern constexpr double BarThicknessFineDialThetaLimit = 1.20;
	extern constexpr double BarThicknessFineDialDepthLiftDip = 4.0;
	extern constexpr double BarThicknessFineDialEdgeFadeStart = 0.68;
	extern constexpr double BarThicknessFineDialTickLengthDip = 7.0;
	extern constexpr double BarThicknessFineDialMajorTickLengthDip = 12.0;
	extern constexpr double BarThicknessFineDialSelectorWidthDip = 7.0;
	extern constexpr double BarThicknessFineDialSelectorHeightDip = 5.0;
	extern constexpr double BarThicknessSliderThumbMorphExitOpacity = 0.04;
	// 拖动改值后静止 0.5s 出提示，再 1.5s（合计 2.0s）进度走满并锁定粗细。
	extern constexpr double BarThicknessHoldHintAnimDur = 0.18;
	extern constexpr double BarThicknessHoldExchangeAnimDur = 0.12;
	// 圆环相对文字行高为 3/5，并比默认 5px 间隙更贴近文字。
	extern constexpr double BarThicknessHoldRingSizeScale = 3.0 / 5.0;
	extern constexpr double BarThicknessHoldRingTextGap = 2.0;
	extern constexpr double BarThicknessTooltipBadgeHeight = 24.0;
extern constexpr double BarThicknessTooltipIconSize = 14.0;
	extern constexpr double BarThicknessTooltipCloseButtonSize = 20.0;
extern constexpr double BarThicknessTooltipHitPadding = 2.0;
extern constexpr double BarThicknessTooltipPadding = 8.0;
extern constexpr double BarThicknessTooltipCloseReserve = 25.0;
extern constexpr double BarThicknessTooltipTitleFontSize = 12.0;
extern constexpr double BarThicknessTooltipBodyFontSize = 10.0;
extern constexpr double BarThicknessTooltipLineGap = 3.0;
extern constexpr double BarThicknessTooltipPopupGap =
	BarDrawAttributeThicknessContentInset;
extern constexpr double BarThicknessTooltipFillOpacity =
	BarDrawAttributeSurfaceOpacity;
extern constexpr double BarThicknessTooltipFrameOpacity = 0.18;
extern constexpr double BarColorSwatchFrameOpacity = 0.18;
extern constexpr double BarColorPickerPanelWidth = 300.0;
// 5 顶距 + 30 顶栏 + 5 间隔 + 132 色板 + 32 读数（含底部 10px 额外间隙）+ 5 底距 = 209。
extern constexpr double BarColorPickerPanelHeight = 209.0;
extern constexpr double BarColorPickerPaletteInset = 5.0;
extern constexpr double BarColorPickerPaletteTop = 40.0;
extern constexpr double BarColorPickerPaletteWidth = 290.0;
extern constexpr double BarColorPickerPaletteHeight = 132.0;
// 与粗细快速调节按钮同高（30px），顶部色系/预览/关闭共用。
extern constexpr double BarColorPickerChromeHeight = 30.0;
extern constexpr double BarColorPickerChromeTop = 5.0;
extern constexpr double BarColorPickerPanelGap = BarDrawAttributeGap;
// 颜色选择器与绘制属性窗口共用默认展开时长，保证回弹节奏一致。
extern constexpr double BarColorPickerPanelAnimationDur = 0.40;
extern constexpr double BarColorPickerCompactScale = BarDrawAttributeCompactScale;
extern constexpr double BarColorPickerCompactWidth =
	BarColorPickerPanelWidth * BarColorPickerCompactScale;
extern constexpr double BarColorPickerCompactHeight =
	BarColorPickerPanelHeight * BarColorPickerCompactScale;
extern constexpr double BarColorPickerHoldHintAnimationDur = 0.18;
extern constexpr double BarMorePanelGap = 5.0;
// 主栏与浮层之间留出更明显的净空；网格单元仍沿用标准 5 DIP 间距。
extern constexpr double BarMorePanelAnchorGap = 12.0;
extern constexpr double BarMorePanelPadding = 5.0;
// 关闭按钮放在网格右侧的窄栏，不再占用面板顶部高度。
extern constexpr double BarMorePanelCloseSideWidth = 35.0;
extern constexpr double BarMorePanelSeparatorGap = 10.0;
extern constexpr double BarMorePanelCompactScale = 0.16;
extern constexpr double BarMorePanelCompactWidth = 60.0;
extern constexpr double BarMorePanelCompactHeight = 30.0;
// ====================
// 媒体

// 媒体操控类
void BarMediaClass::LoadFormat()
{
	formatCache = make_unique<BarFormatCache>(
		Inkeys::UI::RenderPipeline::DWriteFactory().Get());
}

// ====================
// 界面

Inkeys::UI::Bar::Ui3FiniteSignature BarUISetClass::ReadFiniteSignature(
	const StateModeVersionedSnapshot& tool)
{
	using namespace Inkeys::UI::Bar;
	Ui3FiniteSignature signature;
	const auto& attribute = barState.drawAttributeBar;
	const bool auxClosed = !attribute.penTypeMenuOpen && !attribute.colorPickerOpen
		&& attribute.thicknessViewMode == ThicknessViewMode::Preview
		&& !attribute.thicknessSliderHover && !attribute.thicknessSliderPinned
		&& !attribute.thicknessSliderPressed && !attribute.thicknessSliderDragging
		&& !attribute.thicknessPreviewDragging && !attribute.thicknessSliderCapture
		&& !attribute.thicknessFineDialDragging && !attribute.thicknessFineDialPhysicsActive
		&& !attribute.thicknessFineDialCandidateActive && !attribute.thicknessFineDialRangeTransitionActive
		&& !attribute.thicknessFineDialActivationPreviewActive && !attribute.thicknessFineDialActivationDwellActive
		&& !attribute.thicknessSliderHoldHintActive && !attribute.thicknessSliderHoldLocked
		&& !attribute.thicknessAnnotationHover && !attribute.thicknessAnnotationHoverGrace
		&& !attribute.thicknessAnnotationPinned && !attribute.thicknessOverflowHover
		&& !attribute.thicknessOverflowHoverGrace && !attribute.thicknessOverflowPinned
		&& !attribute.colorPickerPointerPressed && !attribute.colorPickerPointerCapture
		&& !attribute.colorPickerHoldHintActive && !attribute.colorPickerHoldLocked
		&& attribute.colorPickerKeyboardDownMask == 0;
	signature.flags = (barState.fold ? 1u : 0u) | (barState.drawAttribute ? 2u : 0u)
		| (barState.geometryAttribute ? 4u : 0u) | (barState.moreExpanded ? 8u : 0u)
		| (barState.eraserAttribute ? 16u : 0u) | (barState.eraserSensitivityOpen ? 32u : 0u)
		| (auxClosed ? 64u : 0u);
	// 空闲签名位记录两项布局配置，不改变既有测试DTO尺寸。
	signature.flags |= BarMainLayoutSignatureFlags(NormalizeBarIconOrder(Inkeys::config.UI.Bar.IconOrder.load()),
		NormalizeBarExpandDirectionAdjustment(Inkeys::config.UI.Bar.ExpandDirectionAdjustment.load()));
	signature.validMask = 1;
	// 宽/色/工具版本来自同一快照，不用独立getter拼接不同代状态。
	signature.stateMode = static_cast<std::uint32_t>(tool.state.StateModeSelect);
	signature.penMode = static_cast<std::uint32_t>(tool.state.Pen.ModeSelect);
	signature.penColorRgb = GetPenColor(tool.state) & 0x00FFFFFFu;
	const float width = GetPenWidth(tool.state);
	signature.penWidthBits = std::bit_cast<std::uint32_t>(width);
	signature.toolRevision = tool.revision;
	if (tool.state.StateModeSelect == StateModeSelectEnum::IdtPen && !tool.state.laserActive
		&& isfinite(width) && width > 0.0f) signature.validMask |= 2;
	signature.mainSide = barState.widgetPosition.mainBar ? 1u : 0u;
	signature.primarySide = barState.widgetPosition.primaryBar ? 1u : 0u;
	signature.validMask |= 4;
	signature.thicknessView = static_cast<std::uint32_t>(attribute.thicknessViewMode.load());
	signature.validMask |= 8;
	signature.darkStyle = barStyle.darkStyle ? 1u : 0u;
	signature.validMask |= 16;
	double uiBaseScale = 0.0;
	// 环境只做一次非阻塞读取；锁忙/未知display不填零冒充有效mask。
	{
		unique_lock displayLock(pendingDisplayPublishMutex, std::try_to_lock);
		if (displayLock)
		{
			const auto serial = pendingDisplaySerial.load(std::memory_order_acquire);
			const auto dpi = pendingDisplayDpi.load(std::memory_order_relaxed);
			if (serial != 0 && (serial & 1ULL) == 0 && dpi != 0)
			{
				signature.displaySerial = serial;
				signature.dpi = dpi;
				uiBaseScale = pendingDisplayUiBaseScale.load(std::memory_order_relaxed);
				signature.validMask |= 32 | 64;
			}
		}
	}
	// wire 保留原字段名，倍率含 UI 基准/DPI 校正；Source 的 DPI×倍率仍还原真实 UI scale。
	const double configZoom = uiBaseScale * USER_DEFAULT_SCREEN_DPI
		/ (signature.dpi ? signature.dpi : USER_DEFAULT_SCREEN_DPI) * barStyle.configZoom;
	signature.configZoomBits = std::bit_cast<std::uint64_t>(configZoom);
	if (isfinite(configZoom) && configZoom > 0.0) signature.validMask |= 128;
	if (Inkeys::UI::Bar::WhiteboardActive() || Inkeys::UI::Bar::PptPresentationActive()) signature.validMask = 0;
	return signature;
}

void BarUISetClass::UpdateRendering(bool updateState)
{
	// 历史/绘制回调的false请求只唤醒，不能等待可能回入其owner的状态锁。
	static mutex mtx;
	if (updateState)
	{
		lock_guard<mutex> lock(mtx);
		// 按钮、面板归位和粗细文字使用同一工具代次，再统一发布渲染请求。
		const auto stateMode = Inkeys::UI::Bar::CurrentUi3FiniteMutation() ? GetStateModeSnapshot() : GetCommittedStateModeSnapshot();
		barButtonSet.StateUpdate(stateMode);
		// 仅在画笔模式刷新粗细文字，收起过程中保留最后一次有效显示。
		if (stateMode.StateModeSelect == StateModeSelectEnum::IdtPen)
			barState.ThicknessDisplayUpdate(GetPenWidthMetrics(stateMode).resolvedWidthDip);
	}

	if (Inkeys::UI::Bar::CurrentUi3FiniteMutation())
	{
		const auto tool = GetStateModeVersionedSnapshot();
		const auto signature = ReadFiniteSignature(tool);
		// 规范化已结束；包括UpdateRendering(false)，仍在原Notify/Request之前封口。
		Inkeys::UI::Bar::FinishCurrentUi3FiniteAtRenderRequest(signature);
	}

	// 通知计算并渲染
	BarAtomic::wait.Notify();
	Inkeys::UI::RenderPipeline::Request(
		Inkeys::UI::RenderPipeline::Client::Bar);
}

// 全局 Bar UI 集合
BarUISetClass barUISet;

// ====================
// 环境

// 初始化

namespace Inkeys::UI::Bar
{
	void SetAnimationOptions(bool enable, double speedRate)
	{
		speedRate = isfinite(speedRate) ? clamp(speedRate, 0.1, 5.0) : 1.0;
		// 使用足够大的有限倍率统一完成普通动画、批次和 SVG 关键帧，不能用 0 让时间轴停住。
		BarUiAnimationEnabled = enable;
		BarUiAnimationSpeedRate = enable ? speedRate : 1.0e12;
		if (!enable) RequestBarBorderCursorSuspend();
		barUISet.UpdateRendering(false);
	}

	void SetEdgeLightingOptions(bool enable, bool dynamic)
	{
		BarUiEdgeLightingEnabled = enable;
		BarUiDynamicEdgeLightingEnabled = dynamic;
		// 关闭任一级动态光门禁时，统一交给 Bar 窗口线程注销 Raw Input。
		if (!enable || !dynamic) RequestBarBorderCursorSuspend();
		barUISet.UpdateRendering(false);
	}

	void SetDebugOptions(bool enable, bool showFrameRate)
	{
		BarUiDebugModeEnabled = enable;
		BarUiDebugFrameRateEnabled = showFrameRate;
		// 渲染线程会比较新旧选项，只在需要时清除 FPS 文字或红框。
		barUISet.UpdateRendering(false);
		Inkeys::UI::RenderPipeline::Request(
			Inkeys::UI::RenderPipeline::WhiteboardMask());
	}

	bool DebugModeEnabled() noexcept
	{
		return BarUiDebugModeEnabled;
	}

	void SetCurrentPageHasContent(bool hasContent) noexcept
	{
		if (static_cast<bool>(currentPageHasContent) == hasContent) return;
		currentPageHasContent = hasContent;
		if (contentStateUpdatesReady.load(std::memory_order_acquire))
			barUISet.UpdateRendering();
	}

	void NoteRecallHistoryAvailability(bool canUndo, bool canRedo)
	{
		barUISet.NoteRecallHistoryAvailability(canUndo, canRedo);
	}

	void RefreshToolState() noexcept
	{
		// 设置切模式即使未改变墨迹或工具，也必须重新计算按钮；早期 CLI 尚未建好预设。
		barUISet.UpdateRendering(contentStateUpdatesReady.load(std::memory_order_acquire));
	}

	bool CurrentPageHasContent() noexcept
	{
		return currentPageHasContent;
	}

	void SetPptPresentationActive(bool active) noexcept
	{
		if (pptPresentationActive.exchange(active, std::memory_order_acq_rel)
			== active) return;
		if (contentStateUpdatesReady.load(std::memory_order_acquire))
			barUISet.UpdateRendering();
	}

	bool PptPresentationActive() noexcept
	{
		return pptPresentationActive.load(std::memory_order_acquire);
	}

	std::uint64_t AutoCollapseSceneRevision() noexcept
	{
		return autoCollapseSceneRevision.load(std::memory_order_acquire);
	}

	void PublishPptSession(std::uint64_t session, bool active, HWND showWindow) noexcept
	{
		bool changed = false;
		bool windowChanged = false;
		{
			std::scoped_lock lock(pptSceneMutex);
			// 先发布完整载荷，再开放场景边沿；渲染线程不能读到旧窗口或旧 dock 条件。
			windowChanged = pptPresentationWindow.exchange(active ? showWindow : nullptr,
				std::memory_order_acq_rel) != (active ? showWindow : nullptr);
			changed = pptPresentationActive.exchange(active, std::memory_order_acq_rel) != active;
			changed = pptSceneState.Publish(session, active) || changed;
		}
		// COM 这里只发布后台事实，稳定 chrome/桌面基线由协调 Commit 的通知更新。
		// 同场窗口锚变化只刷新布局，不重放场次进入和位置恢复。
		if (changed || windowChanged)
		{
			barUISet.UpdateRendering(
				contentStateUpdatesReady.load(std::memory_order_acquire));
		}
		// 场次边沿是「仅放映时显示」的唯一权威信号；窗口变化不改变显隐。
		if (changed) ApplyPresentationVisibility();
	}
	void InitializePresentationVisibility() noexcept
	{
		// 启动阶段是否显示主栏由 Window.Legacy 按同一开关决定，这里对齐初始事实。
		presentationVisibilityHidden.store(
			Inkeys::config.UI.Bar.HideWhenNotPresenting.load(),
			std::memory_order_release);
	}

	void ApplyPresentationVisibility() noexcept
	{
		// 只有场次边沿与设置写入会走到这里，命令幂等，重复下发不会闪窗。
		const bool shouldHide = Inkeys::config.UI.Bar.HideWhenNotPresenting.load()
			&& !PptPresentationActive();
		if (presentationVisibilityHidden.exchange(shouldHide,
			std::memory_order_acq_rel) == shouldHide) return;
		auto& service = Inkeys::Window::GetService();
		if (shouldHide)
		{
			(void)service.Hide(Inkeys::Window::WindowRole::Bar);
			return;
		}
		(void)service.Show(Inkeys::Window::WindowRole::Bar);
		barUISet.UpdateRendering(true);
	}

	void ApplyBarOpacitySettings() noexcept
	{
		// 整窗 alpha 由 StartupPreview 在启动阶段独占，这里不能抢。
		if (Inkeys::UI::StartupPreview::ShouldBarStartTransparent()) return;
		const double effective = std::clamp(ClampedBarOpacityPercent() / 100.0, 0.0, 1.0);
		const auto alpha = static_cast<std::uint8_t>(
			std::clamp(std::lround(effective * 255.0), 0L, 255L));
		RequestPresentationAlpha(alpha);
	}

	void ScheduleBarOpacityApply() noexcept
	{
		// 只做一次延迟落地；启动预览结束后渲染线程可能已经休眠，需要计时器唤醒。
		if (!floating_window || !IsWindow(floating_window)) return;
		SetTimer(floating_window, BarOpacityApplyTimerId, 4000, nullptr);
	}

	namespace
	{
		[[nodiscard]] NOTIFYICONDATAW MakeTrayData(HWND owner, HICON icon) noexcept
		{
			NOTIFYICONDATAW data{};
			data.cbSize = sizeof(data);
			data.hWnd = owner;
			data.uID = BarTrayIconId;
			data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
			data.uCallbackMessage = BarTrayCallbackMessage;
			data.hIcon = icon;
			wcscpy_s(data.szTip, L"Inkeys");
			return data;
		}

		void ExecuteTrayCommand(UINT command) noexcept
		{
			switch (command)
			{
			case BarTrayMenuOpenSettings:
				Inkeys::UI::Setting::Toggle();
				break;
			case BarTrayMenuOnlyPresenting:
			{
				const bool next = !Inkeys::config.UI.Bar.HideWhenNotPresenting.load();
				Inkeys::config.UI.Bar.HideWhenNotPresenting = next;
				(void)Inkeys::config.Write();
				ApplyPresentationVisibility();
				break;
			}
			case BarTrayMenuExit:
				CloseProgram();
				break;
			default:
				break;
			}
		}

		void ShowTrayMenu(HWND owner) noexcept
		{
			const HMENU menu = CreatePopupMenu();
			if (!menu) return;
			AppendMenuW(menu, MF_STRING, BarTrayMenuOpenSettings, L"打开设置");
			AppendMenuW(menu, MF_STRING | (Inkeys::config.UI.Bar.HideWhenNotPresenting.load()
				? MF_CHECKED : MF_UNCHECKED), BarTrayMenuOnlyPresenting, L"仅在放映时显示工具栏");
			AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
			AppendMenuW(menu, MF_STRING, BarTrayMenuExit, L"退出");
			POINT cursor{};
			GetCursorPos(&cursor);
			// 托盘菜单必须先把 owner 提到前台，否则点击别处不会收起。
			SetForegroundWindow(owner);
			const UINT command = TrackPopupMenu(menu,
				TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, cursor.x, cursor.y, 0, owner, nullptr);
			DestroyMenu(menu);
			PostMessageW(owner, WM_NULL, 0, 0);
			if (command) ExecuteTrayCommand(command);
		}
	}

	void InitializeBarTray() noexcept
	{
		if (trayReady.load(std::memory_order_acquire)) return;
		if (!floating_window || !IsWindow(floating_window)) return;
		HICON icon = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr),
			MAKEINTRESOURCEW(IDI_ICON1), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE | LR_SHARED));
		if (!icon) icon = LoadIconW(nullptr, IDI_APPLICATION);
		auto data = MakeTrayData(floating_window, icon);
		if (!Shell_NotifyIconW(NIM_ADD, &data))
		{
			if (IDTLogger) IDTLogger->warn("[托盘] 注册图标失败，错误码 {}", GetLastError());
			return;
		}
		trayReady.store(true, std::memory_order_release);
	}

	void ShutdownBarTray() noexcept
	{
		if (!trayReady.exchange(false, std::memory_order_acq_rel)) return;
		if (!floating_window || !IsWindow(floating_window)) return;
		auto data = MakeTrayData(floating_window, nullptr);
		(void)Shell_NotifyIconW(NIM_DELETE, &data);
	}

	bool HandleBarTrayMessage(HWND hwnd, WPARAM wParam, LPARAM lParam) noexcept
	{
		if (!trayReady.load(std::memory_order_acquire)) return false;
		if (static_cast<UINT>(wParam) != BarTrayIconId) return false;
		switch (static_cast<UINT>(lParam))
		{
		case WM_LBUTTONUP:
		case WM_LBUTTONDBLCLK:
			Inkeys::UI::Setting::Toggle();
			return true;
		case WM_RBUTTONUP:
		case WM_CONTEXTMENU:
			ShowTrayMenu(hwnd);
			return true;
		default:
			return false;
		}
	}

	bool HandleBarTaskbarCreated() noexcept
	{
		// 资源管理器重启后托盘图标会丢失，需要按同一份数据重新注册。
		if (!trayReady.load(std::memory_order_acquire)) return false;
		if (!floating_window || !IsWindow(floating_window)) return false;
		HICON icon = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr),
			MAKEINTRESOURCEW(IDI_ICON1), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE | LR_SHARED));
		if (!icon) icon = LoadIconW(nullptr, IDI_APPLICATION);
		auto data = MakeTrayData(floating_window, icon);
		(void)Shell_NotifyIconW(NIM_ADD, &data);
		return true;
	}

	std::optional<bool> ConsumePptSceneTransition() noexcept
	{
		std::scoped_lock lock(pptSceneMutex);
		const auto transition = pptSceneState.Take();
		if (transition.has_value())
			pptDockSceneActive.store(pptSceneState.active, std::memory_order_release);
		return transition;
	}
	HWND PptPresentationWindow() noexcept
	{
		return pptPresentationWindow.load(std::memory_order_acquire);
	}
	HMONITOR PptSceneMonitor() noexcept
	{
		return pptSceneMonitor.load(std::memory_order_acquire);
	}
	void UpdateSceneMonitor(HMONITOR monitor) noexcept
	{
		pptSceneMonitor.store(monitor, std::memory_order_release);
		barUISet.PublishDisplaySnapshot(Inkeys::Display::GetSnapshot());
	}
	void UpdatePptSceneMonitor(HWND window) noexcept
	{
		UpdateSceneMonitor(window && IsWindow(window)
			? MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST) : nullptr);
	}
	double SceneBottomDockInsetDip() noexcept
	{
		const auto workspace = GetWorkspaceSceneSnapshot();
		return ResolveBarWorkspaceBottomInsetDip(barUISet.barState.fold,
			workspace.committed == WorkspaceScene::Whiteboard, workspace.committed == WorkspaceScene::Presentation,
			workspace.committedMinimized);
	}
	bool PptBusinessFocusAllowed() noexcept
	{
		return PptPresentationActive() && !GetWorkspaceSceneSnapshot().pending && !WhiteboardActive()
			&& !barUISet.barState.drawAttributeBar.colorPickerOpen
			&& !barUISet.barState.eraserAttribute;
	}
	void SetBusinessFocusCallback(std::function<void()> callback)
	{
		std::scoped_lock lock(pptBusinessFocusMutex);
		pptBusinessFocusCallback = std::move(callback);
	}
	void NotifyPptBusinessAction()
	{
		if (!PptBusinessFocusAllowed()) return;
		std::function<void()> callback;
		{
			std::scoped_lock lock(pptBusinessFocusMutex);
			callback = pptBusinessFocusCallback;
		}
		if (callback) callback();
	}
	void SetEndShowRequestCallback(std::function<void(std::uint64_t)> callback)
	{
		endShowDispatcher.SetStamped(std::move(callback));
	}
	void CompleteEndShowRequest(std::uint64_t request) noexcept
	{
		endShowDispatcher.Complete(request);
	}

	void SetEndShowCallback(std::function<void()> callback)
	{
		endShowDispatcher.Set(std::move(callback));
	}

	void RequestEndShow()
	{
		// 回调只投递原 PPT 业务队列，不能在 Bar 输入线程持锁执行。
		(void)endShowDispatcher.Dispatch();
	}

	void CompleteEndShowRequest() noexcept
	{
		endShowDispatcher.Complete();
	}

	void SetWhiteboardActive(bool active) noexcept
	{
		const auto scene = GetWorkspaceSceneSnapshot().committed;
		active = scene == WorkspaceScene::Whiteboard;
		const bool whiteboardChanged = whiteboardActive.exchange(active, std::memory_order_acq_rel) != active;
		const bool changed = committedChromeScene.exchange(scene, std::memory_order_acq_rel) != scene;
		if (changed)
		{
			autoCollapseSceneRevision.fetch_add(1, std::memory_order_acq_rel);
			barUISet.SettleRecallChromeForWorkspace();
		}
		// 此缓存仅去重渲染通知；工作区真值始终读统一 Committed。
		if (whiteboardChanged && !active) whiteboardDockLockActive.store(false, std::memory_order_release);
		if (changed || contentStateUpdatesReady.load(std::memory_order_acquire)) barUISet.UpdateRendering();
	}

	void CollapseAuxiliaryPanels(bool cancelCapture) noexcept
	{
		barUISet.CollapseAuxiliaryPanels(cancelCapture);
	}

	bool WhiteboardActive() noexcept
	{
		return GetWorkspaceSceneSnapshot().committed == WorkspaceScene::Whiteboard;
	}

	void RequestWhiteboardBottomDock() noexcept
	{
		whiteboardDockLockActive.store(true, std::memory_order_release);
		whiteboardBottomDockRequested.store(true, std::memory_order_release);
		if (contentStateUpdatesReady.load(std::memory_order_acquire))
			barUISet.UpdateRendering(false);
	}

	bool ConsumeWhiteboardBottomDockRequest() noexcept
	{
		return whiteboardBottomDockRequested.exchange(false,
			std::memory_order_acq_rel);
	}

	bool WhiteboardDockLockActive() noexcept
	{
		return whiteboardDockLockActive.load(std::memory_order_acquire);
	}

	void ClearWhiteboardDockLock() noexcept
	{
		whiteboardBottomDockRequested.store(false, std::memory_order_release);
		whiteboardDockLockActive.store(false, std::memory_order_release);
	}

	bool HideWhiteboardSnapIndicator() noexcept
	{
		// 白板沿用真实拖动的资格门禁；程序入场/恢复不会建立吸附提示资格。
		return false;
	}

	void SetContentStateUpdatesReady(bool ready) noexcept
	{
		contentStateUpdatesReady.store(ready, std::memory_order_release);
		// 初始化窗口内可能已收到内容变化，再做一次完整状态同步。
		if (ready) barUISet.UpdateRendering();
	}


}
