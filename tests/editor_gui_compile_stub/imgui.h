#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdarg>
using ImU32 = std::uint32_t;
struct ImVec2 { float x{}, y{}; constexpr ImVec2()=default; constexpr ImVec2(float a,float b):x(a),y(b){} };
struct ImVec4 { float x{}, y{}, z{}, w{}; constexpr ImVec4()=default; constexpr ImVec4(float a,float b,float c,float d):x(a),y(b),z(c),w(d){} };
struct ImDrawData {};
struct ImFont {};
struct ImGuiPayload { const void* Data{}; int DataSize{}; bool IsDataType(const char*) const { return true; } };
struct ImGuiIO {
    ImVec2 DisplaySize{}; ImVec2 MousePos{}; ImVec2 MouseDelta{}; float MouseWheel{};
    bool KeyCtrl{}, KeyShift{}, KeyAlt{}, WantCaptureMouse{}, WantCaptureKeyboard{}, WantTextInput{};
    bool MouseDown[5]{}; float DeltaTime{1.0f/60.0f}; float Framerate{60.0f};
};
struct ImGuiStyle {
    ImVec2 WindowPadding{}, FramePadding{}, ItemSpacing{}, ItemInnerSpacing{}, CellPadding{};
    float Alpha{1.0f}, WindowRounding{}, ChildRounding{}, FrameRounding{}, PopupRounding{}, ScrollbarRounding{}, GrabRounding{}, TabRounding{}, ScrollbarSize{};
    ImVec4 Colors[128]{};
    void ScaleAllSizes(float) {}
};
struct ImDrawVert { ImVec2 pos; };
struct ImDrawVertexFixture {
    int Size{};
    ImDrawVert& operator[](int){static ImDrawVert vertex;return vertex;}
};
struct ImDrawList {
    ImDrawVertexFixture VtxBuffer;
    void AddLine(ImVec2, ImVec2, ImU32, float=1.0f) {}
    void AddRect(ImVec2, ImVec2, ImU32, float=0.0f, int=0, float=1.0f) {}
    void AddRectFilled(ImVec2, ImVec2, ImU32, float=0.0f, int=0) {}
    void AddCircle(ImVec2, float, ImU32, int=0, float=1.0f) {}
    void AddCircleFilled(ImVec2, float, ImU32, int=0) {}
    void AddTriangle(ImVec2, ImVec2, ImVec2, ImU32, float=1.0f) {}
    void AddTriangleFilled(ImVec2, ImVec2, ImVec2, ImU32) {}
    void AddQuadFilled(ImVec2, ImVec2, ImVec2, ImVec2, ImU32) {}
    void AddBezierCubic(ImVec2, ImVec2, ImVec2, ImVec2, ImU32, float, int=0) {}
    void AddText(ImVec2, ImU32, const char*) {}
    void PushClipRect(ImVec2, ImVec2, bool=false) {}
    void PopClipRect() {}
};
struct ImGuiListClipper { int DisplayStart{}, DisplayEnd{}; void Begin(int, float=-1.0f){} bool Step(){return false;} void End(){} };
using ImGuiWindowFlags=int; using ImGuiChildFlags=int; using ImGuiTableFlags=int; using ImGuiTableColumnFlags=int;
using ImGuiSelectableFlags=int; using ImGuiInputTextFlags=int; using ImGuiTreeNodeFlags=int; using ImGuiDragDropFlags=int;
using ImGuiTabItemFlags=int; using ImGuiStyleVar=int; using ImGuiCond=int; using ImGuiCol=int; using ImGuiKey=int; using ImGuiMouseButton=int; using ImGuiMouseCursor=int; using ImGuiDataType=int;
inline constexpr int ImGuiWindowFlags_NoMove=1<<0, ImGuiWindowFlags_NoResize=1<<1, ImGuiWindowFlags_NoCollapse=1<<2,
 ImGuiWindowFlags_AlwaysAutoResize=1<<3, ImGuiWindowFlags_AlwaysVerticalScrollbar=1<<4, ImGuiWindowFlags_HorizontalScrollbar=1<<5,
 ImGuiWindowFlags_NoDecoration=1<<6, ImGuiWindowFlags_NoFocusOnAppearing=1<<7, ImGuiWindowFlags_NoInputs=1<<8,
 ImGuiWindowFlags_NoNav=1<<9, ImGuiWindowFlags_NoNavInputs=1<<10, ImGuiWindowFlags_NoSavedSettings=1<<11,
 ImGuiWindowFlags_NoScrollWithMouse=1<<12, ImGuiWindowFlags_NoScrollbar=1<<13;
inline constexpr int ImGuiChildFlags_AutoResizeY=1, ImGuiChildFlags_Border=2;
inline constexpr int ImGuiTableFlags_Borders=1, ImGuiTableFlags_BordersInnerV=2, ImGuiTableFlags_NoSavedSettings=4,
 ImGuiTableFlags_RowBg=8, ImGuiTableFlags_ScrollY=16, ImGuiTableFlags_SizingStretchProp=32;
inline constexpr int ImGuiTableColumnFlags_WidthStretch=1;
inline constexpr int ImGuiSelectableFlags_SpanAllColumns=1;
inline constexpr int ImGuiInputTextFlags_CharsHexadecimal=1, ImGuiInputTextFlags_EnterReturnsTrue=2;
inline constexpr int ImGuiTreeNodeFlags_DefaultOpen=1;
inline constexpr int ImGuiDragDropFlags_SourceAllowNullID=1;
inline constexpr int ImGuiCond_Always=1, ImGuiCond_FirstUseEver=2;
inline constexpr int ImGuiTabBarFlags_FittingPolicyScroll=1;
inline constexpr int ImGuiWindowFlags_MenuBar=1<<14;
inline constexpr int ImGuiStyleVar_WindowPadding=1, ImGuiStyleVar_FramePadding=2, ImGuiTabItemFlags_SetSelected=1;
inline constexpr int ImGuiCol_TextDisabled=3,ImGuiCol_WindowBg=4,ImGuiCol_PopupBg=5,ImGuiCol_FrameBg=6,
 ImGuiCol_FrameBgHovered=7,ImGuiCol_FrameBgActive=8,ImGuiCol_TitleBg=9,ImGuiCol_TitleBgActive=10,
 ImGuiCol_MenuBarBg=11,ImGuiCol_Button=12,ImGuiCol_ButtonHovered=13,ImGuiCol_ButtonActive=14,
 ImGuiCol_Header=15,ImGuiCol_HeaderHovered=16,ImGuiCol_HeaderActive=17,ImGuiCol_Tab=18,
 ImGuiCol_TabHovered=19,ImGuiCol_TabActive=20,ImGuiCol_CheckMark=21,ImGuiCol_SliderGrab=22,
 ImGuiCol_SliderGrabActive=23,ImGuiCol_Separator=24,ImGuiCol_ResizeGrip=25;
inline constexpr int ImGuiCol_Border=0, ImGuiCol_ChildBg=1, ImGuiCol_Text=2;
inline constexpr int ImGuiKey_D=1, ImGuiKey_Delete=2, ImGuiKey_DownArrow=3, ImGuiKey_E=4, ImGuiKey_LeftArrow=5,
 ImGuiKey_RightArrow=6, ImGuiKey_UpArrow=7, ImGuiKey_W=8, ImGuiKey_Y=9, ImGuiKey_Z=10, ImGuiKey_S=11, ImGuiKey_Insert=12, ImGuiKey_PageUp=13, ImGuiKey_PageDown=14, ImGuiKey_Home=15, ImGuiKey_End=16, ImGuiKey_Escape=17, ImGuiKey_N=18, ImGuiKey_O=19;
inline constexpr int ImGuiMouseButton_Left=0, ImGuiMouseButton_Middle=1, ImGuiMouseButton_Right=2;
inline constexpr int ImGuiMouseCursor_None=-1;
inline constexpr int ImGuiDataType_U32=1, ImGuiDataType_U8=2;
#define IM_COL32(R,G,B,A) ((ImU32)(((A)<<24)|((B)<<16)|((G)<<8)|(R)))
#define IM_COL32_WHITE IM_COL32(255,255,255,255)
namespace ImGui {
inline void PushItemWidth(float) {}
inline void PopItemWidth() {}
inline ImGuiIO& GetIO(){ static ImGuiIO v; return v; }
inline ImGuiStyle& GetStyle(){ static ImGuiStyle v; return v; }
inline ImDrawList* GetBackgroundDrawList(){ static ImDrawList d; return &d; }
inline ImDrawList* GetForegroundDrawList(){ static ImDrawList d; return &d; }
inline ImDrawList* GetWindowDrawList(){ static ImDrawList d; return &d; }
inline const ImGuiPayload* GetDragDropPayload(){ static ImGuiPayload p; return &p; }
inline ImDrawData* GetDrawData(){ static ImDrawData d; return &d; }
inline ImVec2 CalcTextSize(const char*, const char* = nullptr, bool=false, float=-1.0f){return {};}
inline ImVec2 GetContentRegionAvail(){return {};}
inline ImVec2 GetCursorScreenPos(){return {};}
inline ImVec2 GetItemRectMax(){return {};}
inline ImVec2 GetItemRectMin(){return {};}
inline ImVec2 GetWindowPos(){return {};}
inline float GetCursorPosX(){return 0.0f;}
inline float GetFrameHeight(){return 20.0f;}
inline double GetTime(){return 0.0;}
inline ImVec4 ColorConvertU32ToFloat4(ImU32){return {};}
inline void SetNextWindowPos(ImVec2, ImGuiCond=0, ImVec2={}) {}
inline void SetNextWindowSize(ImVec2, ImGuiCond=0) {}
inline void SetNextWindowSizeConstraints(ImVec2, ImVec2) {}
inline bool InvisibleButton(const char*, ImVec2, int=0) { return true; }
inline bool BeginChild(const char*, ImVec2={}, ImGuiChildFlags=0, ImGuiWindowFlags=0) { return true; }
inline void ProgressBar(float, ImVec2 = {}, const char* = nullptr) {}
inline bool Button(const char*, ImVec2 = {}) { return true; }
inline bool InputFloat(const char*, float*, float=0, float=0, const char* = "%.3f", ImGuiInputTextFlags=0) { return true; }
inline bool InputTextMultiline(const char*, char*, std::size_t, ImVec2 = {}, ImGuiInputTextFlags = 0) { return true; }
inline void TextColored(ImVec4, const char*, ...) {}
#define IMGUI_BOOL_FN(name) template<class... A> inline bool name(A&&...){return true;}
#define IMGUI_VOID_FN(name) template<class... A> inline void name(A&&...){}
IMGUI_BOOL_FN(Begin) IMGUI_BOOL_FN(BeginCombo) IMGUI_BOOL_FN(BeginDragDropSource)
IMGUI_BOOL_FN(BeginMenuBar) IMGUI_BOOL_FN(BeginMainMenuBar) IMGUI_BOOL_FN(BeginMenu) IMGUI_BOOL_FN(BeginPopupModal) IMGUI_BOOL_FN(BeginTabBar)
IMGUI_BOOL_FN(BeginTabItem) IMGUI_BOOL_FN(BeginTable) IMGUI_BOOL_FN(BeginTooltip)
IMGUI_BOOL_FN(Checkbox) IMGUI_BOOL_FN(CollapsingHeader) IMGUI_BOOL_FN(ColorEdit4) IMGUI_BOOL_FN(Combo)
IMGUI_BOOL_FN(DragFloat) IMGUI_BOOL_FN(DragFloat3) IMGUI_BOOL_FN(InputInt) IMGUI_BOOL_FN(InputScalar)
IMGUI_BOOL_FN(InputText) IMGUI_BOOL_FN(InputTextWithHint) IMGUI_BOOL_FN(IsAnyItemActive) IMGUI_BOOL_FN(IsItemClicked) IMGUI_BOOL_FN(IsItemHovered) IMGUI_BOOL_FN(IsKeyDown)
IMGUI_BOOL_FN(IsKeyPressed) IMGUI_BOOL_FN(IsMouseClicked) IMGUI_BOOL_FN(IsMouseDown) IMGUI_BOOL_FN(IsMouseDragging)
IMGUI_BOOL_FN(IsMouseReleased) IMGUI_BOOL_FN(MenuItem) IMGUI_BOOL_FN(RadioButton) IMGUI_BOOL_FN(Selectable)
IMGUI_BOOL_FN(SliderFloat) IMGUI_BOOL_FN(SliderInt) IMGUI_BOOL_FN(SmallButton) IMGUI_BOOL_FN(TreeNodeEx) IMGUI_BOOL_FN(TreeNode)
IMGUI_VOID_FN(BulletText) IMGUI_VOID_FN(CloseCurrentPopup) IMGUI_VOID_FN(End) IMGUI_VOID_FN(EndChild)
IMGUI_VOID_FN(EndCombo) IMGUI_VOID_FN(EndDragDropSource) IMGUI_VOID_FN(EndMenuBar) IMGUI_VOID_FN(EndMainMenuBar) IMGUI_VOID_FN(EndMenu)
IMGUI_VOID_FN(EndPopup) IMGUI_VOID_FN(EndTabBar) IMGUI_VOID_FN(EndTabItem) IMGUI_VOID_FN(EndTable) IMGUI_VOID_FN(EndTooltip)
IMGUI_VOID_FN(NewFrame) IMGUI_VOID_FN(OpenPopup) IMGUI_VOID_FN(PopID) IMGUI_VOID_FN(PopStyleColor) IMGUI_VOID_FN(PopTextWrapPos)
IMGUI_VOID_FN(StyleColorsDark) IMGUI_VOID_FN(PushStyleVar) IMGUI_VOID_FN(PopStyleVar) IMGUI_VOID_FN(PushID) IMGUI_VOID_FN(PushStyleColor) IMGUI_VOID_FN(PushTextWrapPos)
IMGUI_VOID_FN(Render) IMGUI_VOID_FN(SameLine) IMGUI_VOID_FN(Separator) IMGUI_VOID_FN(SeparatorText) IMGUI_VOID_FN(SetDragDropPayload)
IMGUI_VOID_FN(SetItemDefaultFocus) IMGUI_VOID_FN(SetMouseCursor) IMGUI_VOID_FN(SetNextItemWidth) IMGUI_VOID_FN(SetNextWindowBgAlpha)
IMGUI_VOID_FN(SetTooltip)
IMGUI_VOID_FN(TableHeadersRow) IMGUI_VOID_FN(TableNextRow) IMGUI_VOID_FN(TableSetColumnIndex) IMGUI_VOID_FN(TableSetupColumn)
IMGUI_VOID_FN(Text) IMGUI_VOID_FN(TextDisabled) IMGUI_VOID_FN(TextUnformatted) IMGUI_VOID_FN(TextWrapped)
IMGUI_VOID_FN(TreePop)
#undef IMGUI_BOOL_FN
#undef IMGUI_VOID_FN
}
