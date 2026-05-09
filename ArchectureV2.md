# Archecture V2

## 1. 修正后的模块边界

当前工程的干净运行时边界应按实际目录和职责理解：

| 模块 | 运行时角色 | 说明 |
| --- | --- | --- |
| `:app` | 主 Android App | `TermuxActivity`、`TermuxService`、终端 UI、桌面容器、保活、脚本入口。 |
| `:termux-x11` | App 内嵌 X11 显示实现 | `LorieView`、native Xlorie、EGL 渲染、输入桥接、X11 preference/controller。 |
| `:terminal-view` | 终端 View | `TerminalView` 自定义 View，Canvas 渲染终端内容。 |
| `:terminal-emulator` | 终端核心 | PTY、fork/exec、VT/xterm escape、screen buffer。 |
| `:termux-shared` | 公共层 | 常量、shell、properties、通知、local socket、命令模型。 |
| `:float-ball` | 辅助 UI | 应用内或 overlay 悬浮球菜单。 |
| `:termux-wayland` | termux-package 生态显示桥 | 服务于生态软件显示桥接；不参与 `:app` 主 UI 运行时。 |
| `:shell-loader` | X11 命令入口加载器 | 从 shell 侧加载 `CmdEntryPoint`，启动 native X server。 |

![V2 模块边界](docs/ArchectureV2/module-boundaries.svg)

## 2. V2 目标

1. `TermuxActivity` 与 X11 runtime 从继承改为组合。
2. `termux-x11` 对 `:app` 暴露可嵌入 UI 控件，而不是 Activity 基类。
3. X11 显示器、终端、X11 设置面板保持在同一个 `TermuxActivity`。
4. X11 server 生命周期归 `TermuxService` 托管，Activity 只 attach/detach 显示面。
5. 保留 `SurfaceView + EGL/GLES + AHardwareBuffer/fd` 渲染链路，避免 Bitmap copy。
6. `termux-wayland` 继续作为生态软件显示桥，不纳入 `:app` 主运行时架构。

## 3. 当前主要耦合点

当前继承耦合已移除：

```java
public class TermuxActivity extends AppCompatActivity implements ServiceConnection, X11DisplayHost
```

当前状态：

- `TermuxActivity` 持有 `LorieViewRuntimeController`，不再继承 `termux-x11` 内的 Activity。
- `TermuxActivity` 在主布局完成后创建 `TermuxScreenView`，attach 到 `MainSurfaceContainer`，再通过 `LorieViewRuntimeController` public API attach X11 显示。
- `LorieView` 已去掉 `MainActivity.getInstance()` / `MainActivity.getPrefs()` 静态入口，JNI 回调通过 `LorieViewRuntimeRegistry` 找到当前 runtime。
- `DisplaySlidingWindow` 已从主布局移除；X11 设置面板移入 `DrawerLayout` 的 `end` drawer。
- `LoriePreferences` 已降级为 preference 命名空间，`LoriePreferenceFragment` 通过 `LorieViewRuntimeApi.Host` 嵌入 `TermuxActivity`。
- X11 输入、广播、软键盘控制、WinHandler 生命周期、Activity window flags、preference apply 流程由 `LorieViewRuntimeController` 和专用 controller 承载。

## 4. 目标结构

可嵌入显示框架放在 `:termux-x11` 内部完成。

建议拆分：

| 类/组件 | 所属模块 | 职责 |
| --- | --- | --- |
| `TermuxScreenView extends FrameLayout` | `:termux-x11` | App 可直接嵌入的 X11 显示控件，内部持有 `LorieView` 和 overlay。 |
| `LorieViewRuntimeApi` | `:termux-x11` | runtime 对 app 和内部组件暴露的 public API / host 接口集合。 |
| `LorieViewRuntimeController` | `:termux-x11` | 普通 Java 对象，承载 X11 lifecycle、输入、广播、窗口、preference 应用逻辑。 |
| `LorieViewRuntimeRegistry` | `:termux-x11` | JNI 回调用的弱引用注册表，替代旧 `MainActivity.getInstance()`。 |
| `LorieViewRuntimeSupport` | `:termux-x11` | package-private runtime helper 集合，包含输入、广播、server connector、软键盘、窗口和 preference controller。 |
| `X11InputController` | `:termux-x11` | touch/key/mouse/stylus/gamepad 到 X server 的输入桥。 |
| `X11SoftKeyboardController` | `:termux-x11` | 管理 IME 显示、外接键盘状态和 X11 焦点请求。 |
| `X11WinHandlerController` | `:termux-x11` | 管理 `WinHandler` 创建、线程启动/停止、任务管理弹窗。 |
| `X11WindowModeController` | `:termux-x11` | 管理 fullscreen、cutout、orientation、keep screen on、system UI flags。 |
| `X11PreferencesController` | `:termux-x11` | 管理 preference change 分发、延迟应用、输入/窗口/IME/辅助按钮状态刷新。 |
| `LoriePreferenceFragment` | `:termux-x11` | 可嵌入设置页，当前放在 `DrawerLayout` 的 `end` drawer。 |

`TermuxActivity` 目标继承关系：

```java
public class TermuxActivity extends AppCompatActivity implements ServiceConnection
```

集成约束：

- `TermuxScreenView` 不直接持有 `TermuxActivity`。
- Activity 能力通过 `X11DisplayHost` 注入，例如 `getActivity()`、`getSupportFragmentManager()`、`openPreference()`、`requestX11Focus()`。
- Activity 生命周期通过 `X11DisplayController.attach(host, view)` / `detach()` 传入。
- `LorieView` 内部只允许短期从 `Context` 查找 Activity，用于 IME、window token、`runOnUiThread()` 等场景；不得保存 Activity 强引用。
- `TermuxActivity implements X11DisplayHost`，只负责 app 集成，不进入 X11 内部状态机。

![Activity 组合架构](docs/ArchectureV2/activity-composition.svg)

## 5. 同 Activity UI 结构

`TermuxActivity` 保持三面板结构，但中间面板不再搬运 X11 Activity root view。

目标 UI：

| 区域 | 内容 |
| --- | --- |
| 左侧 | 终端、session list、toolbar。 |
| 中间 | `TermuxScreenView`，内部包含 `LorieView`、not connected stub、输入 overlay、X11 EK bar。 |
| 右侧 | `X11PreferencePanel`。 |

![同 Activity UI 结构](docs/ArchectureV2/same-activity-ui.svg)

## 6. 主内容区共享方案

另一种更贴近现有布局的方案，是复用 `activity_termux.xml` 里的现有 `DrawerLayout`，把主内容区从单一 `TerminalView` 改成共享容器。

目标结构：

```text
DrawerLayout
  content:
    MainSurfaceContainer(FrameLayout)
      TerminalView
      TermuxScreenView
  start drawer:
    session list
    terminal/display switch control
    existing terminal actions
```

不建议用 Fragment 切换 `TerminalView` 和 `TermuxScreenView`。这不是导航页，而是两个强状态显示面；`FrameLayout + visibility/controller` 更直接，也能减少 `SurfaceView` 被 fragment transaction 反复销毁重建。

切换规则：

- Terminal mode：`TerminalView` 可见，`TermuxScreenView` 隐藏，drawer 边缘手势可按现状启用。
- Display mode：`TermuxScreenView` 可见，`TerminalView` 隐藏或降级后台，X11 获得焦点和输入。
- 新 display connection 到达时，`X11ServerConnector` 更新 `X11DisplayController` 连接状态，由 `MainSurfaceController.showDisplay()` 切到 display surface。
- start drawer 内新增明确控件控制 `TerminalView` / `TermuxScreenView` 显示状态。

## 7. Drawer 手势策略

普通 `DrawerLayout` 的侧栏手势不是系统级语义，而是控件自己的边缘拖拽判定：

```text
ACTION_DOWN 从左/右屏幕边缘开始
横向移动超过 touch slop
横向位移大于纵向位移
DrawerLayout 开始拦截事件并拖出 drawer
ACTION_UP 根据位移和速度决定打开或关闭
```

这会和 `TermuxScreenView` 的边缘输入冲突，尤其是窗口拖拽、游戏输入、触控板模式、右键模拟、全屏应用边缘操作，以及 Android 10+ 系统返回手势。

V2 约束：

- Display mode 下默认关闭 `DrawerLayout` 边缘拖拽。
- 侧栏只能通过明确 UI 打开，例如 start drawer 内切换按钮、X11 overlay 按钮、FloatBall 菜单或硬键入口。
- Terminal mode 下保留当前 terminal drawer 手势。
- 如需代码打开 drawer，先临时 unlock，打开后按当前 mode 恢复 lock。

建议抽象：

```text
MainSurfaceController
  showTerminal()
    -> drawer unlocked
    -> terminal focus

  showDisplay()
    -> drawer locked closed
    -> x11 focus

  openTerminalDrawerExplicitly()
    -> temporary unlock
    -> openDrawer(START)
    -> on closed: restore display lock
```

## 8. 旧 DisplaySlidingWindow 解锁语义

旧 `DisplaySlidingWindow` 曾用两个布尔值控制横向滑动：

| 字段 | 当前含义 |
| --- | --- |
| `mLockContentSlider = false` | 中间 X11 内容接收触摸；滑动窗口不接管横向事件。 |
| `mLockContentSlider = true` | 允许 `HorizontalScrollView` 接管横向滑动，可拖出左右面板。 |
| `mMenuSwitchSlider = true` | 菜单切换态，处理边缘/菜单打开后的触摸状态。 |

关键方法：

```text
showContent()
  -> 回到 center
  -> mLockContentSlider = false
  -> X11 优先接收触摸

setTerminalViewSwitchSlider(true)
  -> 打开左侧终端面板
  -> mLockContentSlider = true

setX11PreferenceSwitchSlider(true)
  -> 打开右侧设置面板
  -> mLockContentSlider = true

releaseSlider(true)
  -> 允许横向滑动窗口重新接管事件
```

V2 主布局已不再使用 `DisplaySlidingWindow`；保留的是这几个语义：

- 保留“Display mode 默认锁住侧边手势”的策略。
- 保留 FloatBall / back / 显式按钮作为“临时解锁或打开菜单”的入口。
- 把 `releaseSlider(true)` 的语义迁移为显式 drawer lock/unlock 或 `openDrawerExplicitly()`。
- 不再复用 `HorizontalScrollView` 的 `mLockContentSlider` / `mMenuSwitchSlider` 实现细节。

## 9. X11 启动与连接

X11 server 的启动仍走当前现实可行链路，不重写 native X server。

V2 的变化是：连接状态由 controller/service 管理，Activity 只作为显示宿主。

![X11 启动与连接](docs/ArchectureV2/x11-startup-connection.svg)

## 10. 渲染与输入路径

渲染路径保持现状，不引入中间 Bitmap：

- X server 生成 root pixmap / cursor。
- native 侧通过 `LorieBuffer`、`AHardwareBuffer` 或 fd-backed buffer 传递。
- renderer 线程用 EGL/GLES 渲染到 `LorieView` 的 `Surface`。
- `surfaceDestroyed` 只 detach `ANativeWindow`，不停止 X server。

![X11 渲染路径](docs/ArchectureV2/render-path.svg)

输入路径由 `X11InputController` 统一管理，`TermuxActivity` 不直接操作 `TouchInputHandler` 内部状态。

![X11 输入路径](docs/ArchectureV2/input-path.svg)

## 11. 生命周期与保活

设计原则：

- X11 server 不绑定 Activity 生命周期。
- `TermuxService` 是 X11 桌面 session 的 owner。
- Activity 重建后重新 attach 显示控件。
- 无 Surface 时暂停渲染输出或 detach window，不杀 X server。
- 桌面活跃时前台服务保持通知；可按配置持有 wake lock / wifi lock。

![生命周期与保活](docs/ArchectureV2/lifecycle-keepalive.svg)

普通后台 Activity 的 Surface 渲染无法由应用可靠保证高帧率；V2 能保证的是 X11 server 不因独立 Activity 后台化而被降级或杀掉。需要持续前台可见渲染时，应使用同 Activity 前台、PiP 或外接显示场景。

## 12. `termux-wayland` 定位

`termux-wayland` 不是 `:app` 主显示层。

它的 V2 定位：

- 对 termux-package 生态提供显示桥接能力。
- 与 X11/renderer 共享 buffer/event 概念时，应共享协议定义或保持 ABI 对齐。
- `:app` 主 Activity 不直接依赖它。

![termux-wayland 定位](docs/ArchectureV2/termux-wayland-position.svg)

## 13. 迁移步骤

建议按低风险顺序迁移：

1. 在 `:termux-x11` 中新增 `TermuxScreenView`，并把 X11 显示区域抽成 `view_x11_display.xml` 供控件和旧 Activity wrapper 共用。已完成。
2. 提取 `MainActivity` 中的连接逻辑为 `X11ServerConnector`。已完成 binder/fd/logcat/retry 连接层抽离，广播入口已拆到 `X11BroadcastReceiver` / `X11BroadcastRegistrar`。
3. 提取输入逻辑为 `X11InputController`。已完成 `MainActivity` 到 `TouchInputHandler/InputEventSender` 的封装，`TouchInputHandler` 已改为依赖 `X11InputHost`。
4. 提取显示状态、stub、EK bar、helper buttons 为 `X11DisplayController` 管理。部分完成，连接状态已进入 controller。
5. `TermuxActivity` 改为组合 `TermuxScreenView`，移除 `extends MainActivity`。已完成，当前继承 `AppCompatActivity` 并持有 `LorieViewRuntimeController`。
6. 将现有 terminal `DrawerLayout` 的 content 改为 `MainSurfaceContainer`，由 controller 切换 `TerminalView` / `TermuxScreenView`。已完成。
7. Display mode 下用 `DrawerLayout.setDrawerLockMode(LOCK_MODE_LOCKED_CLOSED)` 锁住边缘拖拽。已完成 start drawer；X11 设置面板走 end drawer 显式打开。
8. `DisplaySlidingWindow` 去掉对 `MainActivity.mLorieViewConnected` 的静态依赖，或在共享容器方案完成后整体下线。已从主布局移除。
9. `LorieView` 去掉对 `MainActivity.getInstance()` / `MainActivity.getPrefs()` 的硬依赖，改为通过 `X11DisplayHost` / `PrefsProvider` 获取 Activity 能力和偏好。静态入口已移除，`LorieView` 已改为依赖 `X11LorieHost`；extra keys、toolbar、win handler、input controls、file picker 已改为宿主接口。
10. 将软键盘状态从 `MainActivity` static 字段迁移到 `X11SoftKeyboardController`。已完成。
11. 将 `WinHandler` 生命周期迁移到 `X11WinHandlerController`，窗口 flags 迁移到 `X11WindowModeController`。已完成。
12. 将 preference change 应用流程迁移到 `X11PreferencesController`。已完成。
13. 将 app 集成回调从 protected `TermuxActivityListener` 改为公开 `X11ActivityIntegration`，并移除 `TermuxActivity` 对 `inputControlsManager` 字段的直接访问。已完成。
14. 将 app 侧对 `openPreference()`、`back2PreviousMenu()`、`onPreferencesChanged()`、`setConnected()` 等旧基类方法的直接调用收敛为 X11 runtime API。已完成。
15. 将 X11 display attach、connection listener 从 protected 初始化/controller 直连收敛为 public runtime API。已完成。
16. 抽出 `LorieViewRuntimeApi` public 边界，`TermuxActivity` 通过 `getLorieViewRuntime()` 调用 X11 能力；当前返回组合持有的 `LorieViewRuntimeController`。已完成。
17. 将 X11 preference/runtime 初始化从隐式 Activity 生命周期中拆出为显式初始化方法。已完成。
18. 将 X11 session 状态上收到 `TermuxService`，Activity 重建后通过 service 恢复连接。
19. 删除旧 wrapper Activity，JNI 改用 `LorieViewRuntimeRegistry` 回调当前 runtime。已完成。

![V2 迁移步骤](docs/ArchectureV2/migration-steps.svg)

## 14. 验收标准

必须满足：

- `TermuxActivity` 不再继承 `com.termux.x11.MainActivity`。
- `:app` 主 UI 不直接依赖 `:termux-wayland`。
- X11 显示器是 `TermuxActivity` 中的普通 View 控件。
- 终端、X11 显示、X11 设置在同一个 Activity 内。
- Activity 重建、旋转、短暂后台不重启 X11 server。
- `surfaceDestroyed` 不等价于停止桌面。
- 渲染链路保持 `SurfaceView + EGL/GLES + AHardwareBuffer/fd`。
- `DisplaySlidingWindow` 不再读取 `MainActivity` 静态连接状态。
- `LorieView` 不再要求存在 `MainActivity.getInstance()`。
- `TermuxScreenView` 不保存 `TermuxActivity` 强引用；Activity 能力只通过 `X11DisplayHost` 使用。
- Display mode 下 drawer 边缘拖拽默认关闭，只能通过显式入口打开侧栏。

## 15. 非目标

- 不重写 Xorg/Xlorie native server。
- 不把 `termux-wayland` 改造成 app 主显示层。
- 不保证普通后台 Activity 的 Surface 持续高帧率；这受 Android 系统调度和可见性限制。
