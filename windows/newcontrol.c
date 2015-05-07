// 6 april 2015
#include "uipriv_windows.h"

struct singleHWND {
	HWND hwnd;
	BOOL (*onWM_COMMAND)(uiControl *, WORD, LRESULT *);
	BOOL (*onWM_NOTIFY)(uiControl *, NMHDR *, LRESULT *);
	void (*onDestroy)(void *);
	void *onDestroyData;
	uiContainer *parent;
	int hidden;
	int userDisabled;
	int containerDisabled;
};

static void singleDestroy(uiControl *c)
{
	struct singleHWND *s = (struct singleHWND *) (c->Internal);

	if (s->parent != NULL)
		complain("attempt to destroy a uiControl at %p while it still has a parent", c);
	(*(s->onDestroy))(s->onDestroyData);
	if (DestroyWindow(s->hwnd) == 0)
		logLastError("error destroying control in singleDestroy()");
	uiFree(s);
}

static uintptr_t singleHandle(uiControl *c)
{
	struct singleHWND *s = (struct singleHWND *) (c->Internal);

	return (uintptr_t) (s->hwnd);
}

static void singleSetParent(uiControl *c, uiContainer *parent)
{
	struct singleHWND *s = (struct singleHWND *) (c->Internal);
	uiContainer *oldparent;
	HWND newParentHWND;

	oldparent = s->parent;
	s->parent = parent;
	newParentHWND = initialParent;
	if (s->parent != NULL)
		newParentHWND = (HWND) uiControlHandle(uiControl(s->parent));
	if (SetParent(s->hwnd, newParentHWND) == NULL)
		logLastError("error setting control parent in singleSetParent()");
	if (oldparent != NULL)
		uiContainerUpdate(oldparent);
	if (s->parent != NULL)
		uiContainerUpdate(s->parent);
}

static void singleResize(uiControl *c, intmax_t x, intmax_t y, intmax_t width, intmax_t height, uiSizing *d)
{
	struct singleHWND *s = (struct singleHWND *) (c->Internal);

	moveAndReorderWindow(s->hwnd, d->Sys->InsertAfter, x, y, width, height);
	d->Sys->InsertAfter = s->hwnd;
}

static int singleVisible(uiControl *c)
{
	struct singleHWND *s = (struct singleHWND *) (c->Internal);

	return !s->hidden;
}

static void singleShow(uiControl *c)
{
	struct singleHWND *s = (struct singleHWND *) (c->Internal);

	ShowWindow(s->hwnd, SW_SHOW);
	s->hidden = 0;
	if (s->parent != NULL)
		uiContainerUpdate(s->parent);
}

static void singleHide(uiControl *c)
{
	struct singleHWND *s = (struct singleHWND *) (c->Internal);

	ShowWindow(s->hwnd, SW_HIDE);
	s->hidden = 1;
	if (s->parent != NULL)
		uiContainerUpdate(s->parent);
}

static void singleEnable(uiControl *c)
{
	struct singleHWND *s = (struct singleHWND *) (c->Internal);

	s->userDisabled = 0;
	if (!s->containerDisabled)
		EnableWindow(s->hwnd, TRUE);
}

static void singleDisable(uiControl *c)
{
	struct singleHWND *s = (struct singleHWND *) (c->Internal);

	s->userDisabled = 1;
	EnableWindow(s->hwnd, FALSE);
}

static void singleSysFunc(uiControl *c, uiControlSysFuncParams *p)
{
	struct singleHWND *s = (struct singleHWND *) (c->Internal);

	switch (p->Func) {
	case uiWindowsSysFuncContainerEnable:
		s->containerDisabled = 0;
		if (!s->userDisabled)
			EnableWindow(s->hwnd, TRUE);
		return;
	case uiWindowsSysFuncContainerDisable:
		s->containerDisabled = 1;
		EnableWindow(s->hwnd, FALSE);
		return;
	case uiWindowsSysFuncHasTabStops:
		if (IsWindowEnabled(s->hwnd) != 0)
			if ((getStyle(s->hwnd) & WS_TABSTOP) != 0)
				p->HasTabStops = TRUE;
		return;
	}
	complain("unknown p->Func %d in singleSysFunc()", p->Func);
}

static LRESULT CALLBACK singleSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
	uiControl *c = (uiControl *) dwRefData;
	struct singleHWND *s = (struct singleHWND *) (c->Internal);
	LRESULT lResult;

	switch (uMsg) {
	case msgCOMMAND:
		if ((*(s->onWM_COMMAND))(c, HIWORD(wParam), &lResult) != FALSE)
			return lResult;
		break;
	case msgNOTIFY:
		if ((*(s->onWM_NOTIFY))(c, (NMHDR *) lParam, &lResult) != FALSE)
			return lResult;
		break;
	case WM_NCDESTROY:
		if ((*fv_RemoveWindowSubclass)(hwnd, singleSubclassProc, uIdSubclass) == FALSE)
			logLastError("error removing Windows control subclass in singleSubclassProc()");
		break;
	}
	return (*fv_DefSubclassProc)(hwnd, uMsg, wParam, lParam);
}

void uiWindowsMakeControl(uiControl *c, uiWindowsMakeControlParams *p)
{
	struct singleHWND *s;

	s = uiNew(struct singleHWND);
	s->hwnd = CreateWindowExW(p->dwExStyle,
		p->lpClassName, p->lpWindowName,
		p->dwStyle | WS_CHILD | WS_VISIBLE,
		0, 0,
		// use a nonzero initial size just in case some control breaks with a zero initial size
		100, 100,
		initialParent, NULL, p->hInstance, NULL);
	if (s->hwnd == NULL)
		logLastError("error creating control in uiWindowsMakeControl()");
	s->onWM_COMMAND = p->onWM_COMMAND;
	s->onWM_NOTIFY = p->onWM_NOTIFY;

	s->onDestroy = p->onDestroy;
	s->onDestroyData = p->onDestroyData;

	if (p->useStandardControlFont)
		SendMessageW(s->hwnd, WM_SETFONT, (WPARAM) hMessageFont, (LPARAM) TRUE);

	// this handles redirected notification messages
	if ((*fv_SetWindowSubclass)(s->hwnd, singleSubclassProc, 0, (DWORD_PTR) c) == FALSE)
		logLastError("error subclassing Windows control in uiWindowsMakeControl()");

	uiControl(c)->Internal = s;
	uiControl(c)->Destroy = singleDestroy;
	uiControl(c)->Handle = singleHandle;
	uiControl(c)->SetParent = singleSetParent;
	uiControl(c)->Resize = singleResize;
	uiControl(c)->Visible = singleVisible;
	uiControl(c)->Show = singleShow;
	uiControl(c)->Hide = singleHide;
	uiControl(c)->Enable = singleEnable;
	uiControl(c)->Disable = singleDisable;
	uiControl(c)->SysFunc = singleSysFunc;
}

char *uiWindowsControlText(uiControl *c)
{
	struct singleHWND *s = (struct singleHWND *) (c->Internal);
	WCHAR *wtext;
	char *text;

	wtext = windowText(s->hwnd);
	text = toUTF8(wtext);
	uiFree(wtext);
	return text;
}

void uiWindowsControlSetText(uiControl *c, const char *text)
{
	struct singleHWND *s = (struct singleHWND *) (c->Internal);
	WCHAR *wtext;

	wtext = toUTF16(text);
	if (SetWindowTextW(s->hwnd, wtext) == 0)
		logLastError("error setting control text in uiWindowsControlSetText()");
	uiFree(wtext);
}
