#pragma once
#include "Common.h"
#include <shellapi.h>
#pragma warning(push)
#pragma warning(disable: 4100 4189 4456 4458)
#include "userwnd.h"
#include "msgmap.h"
#pragma warning(pop)
#include "ui/Controller.h"
#include "ui/Theme.h"
#include "ui/Dialogs.h"
#include "resource.h"

namespace ds {

class CMainWindow;

// Scrollable middle pane. All document controls and document painting live here so
// they clip to this HWND and move together. Header and footer stay on CMainWindow.
class CBodyWindow : public TWL::CUserWindow {
public:
    CMainWindow* host = nullptr;

    LPCTSTR GetClassName() override { return TEXT("DesktopSplitterBody"); }
    void SetWindowClassEx(WNDCLASSEX& wce) override;
    void PreCreate(LPCREATESTRUCT pcs) override;

    BEGIN_MSG_MAP()
        MESSAGE_HANDLER(WM_PAINT, OnPaint)
        MESSAGE_HANDLER(WM_ERASEBKGND, OnErase)
        MESSAGE_HANDLER(WM_SIZE, OnSize)
        MESSAGE_HANDLER(WM_VSCROLL, OnVScroll)
        MESSAGE_HANDLER(WM_MOUSEWHEEL, OnMouseWheel)
        MESSAGE_HANDLER(WM_LBUTTONDOWN, OnLButtonDown)
        MESSAGE_HANDLER(WM_LBUTTONUP, OnLButtonUp)
        MESSAGE_HANDLER(WM_MOUSEMOVE, OnMouseMove)
        MESSAGE_HANDLER(WM_SETCURSOR, OnSetCursor)
        MESSAGE_HANDLER(WM_COMMAND, OnCommand)
        MESSAGE_HANDLER(WM_CTLCOLORSTATIC, OnCtlColor)
        MESSAGE_HANDLER(WM_CTLCOLOREDIT, OnCtlColor)
        MESSAGE_HANDLER(WM_CTLCOLORBTN, OnCtlColor)
        MESSAGE_HANDLER(WM_CTLCOLORLISTBOX, OnCtlColor)
        MESSAGE_HANDLER(WM_DRAWITEM, OnDrawItem)
        MESSAGE_HANDLER(WM_MEASUREITEM, OnMeasureItem)
    END_MSG_MAP()

private:
    bool OnPaint(UINT, WPARAM, LPARAM, LRESULT& lResult);
    bool OnErase(UINT, WPARAM, LPARAM, LRESULT& lResult);
    bool OnSize(UINT, WPARAM, LPARAM, LRESULT& lResult);
    bool OnVScroll(UINT, WPARAM, LPARAM, LRESULT& lResult);
    bool OnMouseWheel(UINT, WPARAM, LPARAM, LRESULT& lResult);
    bool OnLButtonDown(UINT, WPARAM, LPARAM, LRESULT& lResult);
    bool OnLButtonUp(UINT, WPARAM, LPARAM, LRESULT& lResult);
    bool OnMouseMove(UINT, WPARAM, LPARAM, LRESULT& lResult);
    bool OnSetCursor(UINT, WPARAM, LPARAM, LRESULT& lResult);
    bool OnCommand(UINT, WPARAM, LPARAM, LRESULT& lResult);
    bool OnCtlColor(UINT, WPARAM, LPARAM, LRESULT& lResult);
    bool OnDrawItem(UINT, WPARAM, LPARAM, LRESULT& lResult);
    bool OnMeasureItem(UINT, WPARAM, LPARAM, LRESULT& lResult);
};

struct DocLayout {
    int width = 0;
    int height = 0;
    RECT rcCombo{}, rcRescan{};
    RECT rcMode1{}, rcMode2{};
    RECT rcStartupBox{}, rcChkStart{}, rcChkAuto{}, rcStartupExpl{};
    RECT rcLayouts{};
    RECT rcLayoutCards[8]{};
    int layoutCardCount = 0;
    RECT rcEditorFrame{}, rcEditor{};
    RECT rcSumNote{};
    bool hasSumNote = false;
    RECT rcSegCard[4]{};
    RECT rcSegW[4]{}, rcSegH[4]{};
    int segCount = 0;
    RECT rcReset{};
    RECT rcHidingPanel{}, rcHidingIntro{};
    RECT rcHideEn{}, rcHideDis{}, rcHideRecheck{};
    RECT rcDisabledReason{};
    bool hasDisabledReason = false;
    RECT rcSectionMonitor{}, rcSectionStartup{}, rcSectionLayout{};
    RECT rcSectionZone{}, rcSectionHiding{};
};

class CMainWindow : public TWL::CUserWindow {
public:
    CMainWindow();
    ~CMainWindow();

    bool CreateMain();
    BOOL PreTranslateMessage(MSG* pMsg);

    void ShowFromTray();
    void HideToTray();
    void ExitApp();

    Controller& GetController() { return ctl_; }

    LPCTSTR GetClassName() override { return TEXT("DesktopSplitterMainWindow"); }
    void SetWindowClassEx(WNDCLASSEX& wce) override;
    void ProcessDpiChangedMessage(WPARAM wParam, LPARAM lParam) override;

    BEGIN_MSG_MAP()
        if (trayCreatedMsg_ && uMsg == trayCreatedMsg_) {
            AddTray();
            lResult = 0;
            return true;
        }
        MESSAGE_HANDLER(WM_CREATE, OnCreate)
        MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
        MESSAGE_HANDLER(WM_CLOSE, OnClose)
        MESSAGE_HANDLER(WM_SIZE, OnSize)
        MESSAGE_HANDLER(WM_PAINT, OnPaint)
        MESSAGE_HANDLER(WM_ERASEBKGND, OnErase)
        MESSAGE_HANDLER(WM_COMMAND, OnCommand)
        MESSAGE_HANDLER(WM_CTLCOLORSTATIC, OnCtlColor)
        MESSAGE_HANDLER(WM_CTLCOLOREDIT, OnCtlColor)
        MESSAGE_HANDLER(WM_CTLCOLORBTN, OnCtlColor)
        MESSAGE_HANDLER(WM_CTLCOLORLISTBOX, OnCtlColor)
        MESSAGE_HANDLER(WM_DRAWITEM, OnDrawItem)
        MESSAGE_HANDLER(WM_MEASUREITEM, OnMeasureItem)
        MESSAGE_HANDLER(WM_DISPLAYCHANGE, OnDisplayChange)
        MESSAGE_HANDLER(WM_TIMER, OnTimer)
        MESSAGE_HANDLER(WM_QUERYENDSESSION, OnQueryEndSession)
        MESSAGE_HANDLER(WM_ENDSESSION, OnEndSession)
        MESSAGE_HANDLER(Controller::WM_DS_WORK_DONE, OnWorkDone)
        MESSAGE_HANDLER(WM_APP + 40, OnConfirmHop)
        MESSAGE_HANDLER(WM_APP + 41, OnTrayCallback)
    END_MSG_MAP()

    void PreCreate(LPCREATESTRUCT pcs) override;

private:
    friend class CBodyWindow;

    bool OnCreate(UINT, WPARAM, LPARAM, LRESULT& lResult);
    bool OnDestroy(UINT, WPARAM, LPARAM, LRESULT& lResult);
    bool OnClose(UINT, WPARAM, LPARAM, LRESULT& lResult);
    bool OnSize(UINT, WPARAM, LPARAM, LRESULT& lResult);
    bool OnPaint(UINT, WPARAM, LPARAM, LRESULT& lResult);
    bool OnErase(UINT, WPARAM, LPARAM, LRESULT& lResult);
    bool OnCommand(UINT, WPARAM, LPARAM, LRESULT& lResult);
    bool OnCtlColor(UINT, WPARAM, LPARAM, LRESULT& lResult);
    bool OnDrawItem(UINT, WPARAM, LPARAM, LRESULT& lResult);
    bool OnMeasureItem(UINT, WPARAM, LPARAM, LRESULT& lResult);
    bool OnVScroll(UINT, WPARAM, LPARAM, LRESULT& lResult);
    bool OnMouseWheel(UINT, WPARAM, LPARAM, LRESULT& lResult);
    bool OnLButtonDown(UINT, WPARAM, LPARAM, LRESULT& lResult);
    bool OnLButtonUp(UINT, WPARAM, LPARAM, LRESULT& lResult);
    bool OnMouseMove(UINT, WPARAM, LPARAM, LRESULT& lResult);
    bool OnSetCursor(UINT, WPARAM, LPARAM, LRESULT& lResult);
    bool OnDisplayChange(UINT, WPARAM, LPARAM, LRESULT& lResult);
    bool OnTimer(UINT, WPARAM, LPARAM, LRESULT& lResult);
    bool OnWorkDone(UINT, WPARAM, LPARAM, LRESULT& lResult);
    bool OnConfirmHop(UINT, WPARAM, LPARAM, LRESULT& lResult);
    bool OnTrayCallback(UINT, WPARAM, LPARAM, LRESULT& lResult);
    bool OnQueryEndSession(UINT, WPARAM, LPARAM, LRESULT& lResult);
    bool OnEndSession(UINT, WPARAM, LPARAM, LRESULT& lResult);

    void RecreateFonts();
    void RecreateChildren();
    void SyncControls();
    void LayoutFrame();
    void LayoutDocument();
    void PlaceBodyChildren();
    void UpdateScroll();
    void RedrawBodyNow();
    void PaintHeader(HDC hdc, const RECT& rc);
    void PaintBody(HDC hdc, const RECT& rc);
    void PaintLayoutPicker(HDC hdc);
    void PaintZoneEditor(HDC hdc);
    void PaintHiding(HDC hdc);
    int Dip(int v) const { return theme::Dip(v, dpi_); }
    int MeasureHeaderHeight(int innerW) const;
    DocLayout MeasureDocument(int width) const;
    RECT Vis(const RECT& doc) const;
    int HitLayout(int x, int y) const;
    int HitSplitter(int x, int y) const;
    HWND BodyHwnd() const { return body_.m_hWnd; }
    void AddTray();
    void RemoveTray();
    void ShowTrayMenu();
    void DrawRoundRect(HDC hdc, RECT rc, int radius, COLORREF fill, COLORREF border, int borderW = 1);
    void DrawCheckbox(const DRAWITEMSTRUCT* dis);
    int ScrollbarWidth() const;
    void ClearBodyNonClientScroll();
    HWND ScrollHwnd() const { return scrollBar_; }
    void InvalidateHeader();
    void InvalidateBody();

    Controller ctl_;
    std::unique_ptr<UiApplyConfirmer> confirmer_;
    std::unique_ptr<UiSpecializationUi> specUi_;
    CBodyWindow body_;
    DocLayout doc_{};

    int dpi_ = 96;
    int scrollY_ = 0;
    int contentH_ = 0;
    bool exiting_ = false;
    bool minimizeToTray_ = true;
    bool dragging_ = false;
    int hoverSplitter_ = -1;
    bool layoutInProgress_ = false;

    HFONT fontTitle_ = nullptr, fontBody_ = nullptr, fontMuted_ = nullptr;
    HFONT fontHeader_ = nullptr, fontSmall_ = nullptr, fontMono_ = nullptr, fontZone_ = nullptr;
    HBRUSH brWindow_ = nullptr, brPanel_ = nullptr, brEdit_ = nullptr;

    HWND scrollBar_ = nullptr;
    HWND comboMonitor_ = nullptr;
    HWND btnRescan_ = nullptr, btnApply_ = nullptr, btnRevert_ = nullptr, btnTray_ = nullptr;
    HWND btnReset_ = nullptr, btnHideEn_ = nullptr, btnHideDis_ = nullptr, btnHideRecheck_ = nullptr;
    HWND chkStart_ = nullptr, chkAuto_ = nullptr;
    HWND status_ = nullptr;
    HWND segWidth_[4]{}, segHeight_[4]{};

    RECT rcHeader_{}, rcBodyHost_{}, rcButtons_{}, rcStatus_{};
    RECT rcEditor_{}, rcLayouts_{};
    RECT rcLayoutCards_[8]{};
    int layoutCardCount_ = 0;

    NOTIFYICONDATAW nid_{};
    bool trayAdded_ = false;
    UINT trayCreatedMsg_ = 0;
    bool displayChangePending_ = false;
};

} // namespace ds
