#include "powershader.h"
#include <windows.h>
#include <commctrl.h>

#include <algorithm>
#include <cctype>
#include <cwctype>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <max.h>
#include <gup.h>
#include <maxapi.h>
#include <plugin.h>
#include <inode.h>
#include <imtledit.h>
#include <GetCOREInterface.h>
#include <maxscript/maxscript.h>
#include <custcont.h>
#include <Materials/MtlBase.h>
#include <Materials/MtlLib.h>
#include <stdmat.h>
#include <bitmap.h>

#define POWER_SHADER_NAME _T("Power Shader")

extern HINSTANCE hInstance;

namespace PowerShader {
namespace {

constexpr wchar_t kSmeNodeViewClass[] = L"DragDropWindow";

bool IsWindowClass(HWND hwnd, const wchar_t* className)
{
    if (!hwnd || !className) return false;
    wchar_t cls[128] = {};
    int len = GetClassNameW(hwnd, cls, static_cast<int>(std::size(cls)));
    return len > 0 && _wcsicmp(cls, className) == 0;
}

HWND FindSmeNodeViewWindowAtPoint(const POINT& screenPos)
{
    HWND h = WindowFromPoint(screenPos);
    while (h)
    {
        if (IsWindowClass(h, kSmeNodeViewClass)) return h;
        HWND parent = GetParent(h);
        if (!parent || parent == h) break;
        h = parent;
    }
    return nullptr;
}

// Try DAD drop on a specific window
bool TryDADDropOn(MtlBase* mb, HWND target)
{
    if (!mb || !target) return false;
    POINT screenPos{}; GetCursorPos(&screenPos);
    IDADWindow* dadWindow = GetIDADWindow(target);
    if (!dadWindow) return false;
    DADMgr* dadMgr = dadWindow->GetDADMgr();
    if (!dadMgr) { ReleaseIDADWindow(dadWindow); return false; }
    POINT clientPos = screenPos;
    ScreenToClient(target, &clientPos);
    ReferenceTarget* dropThis = static_cast<ReferenceTarget*>(mb);
    const SClass_ID type = mb->SuperClassID();
    const BOOL ok = dadMgr->OkToDrop(dropThis, nullptr, target, clientPos, type, FALSE);
    if (ok) dadMgr->Drop(dropThis, target, clientPos, type, nullptr, FALSE);
    ReleaseIDADWindow(dadWindow);
    return ok == TRUE;
}

// Try DAD drop — SME first, then medit palette sample slots
bool TryDADDrop(MtlBase* mb)
{
    if (!mb) return false;
    POINT screenPos{}; GetCursorPos(&screenPos);

    // 1. SME node view (known safe DAD target)
    HWND smeHwnd = FindSmeNodeViewWindowAtPoint(screenPos);
    if (smeHwnd && TryDADDropOn(mb, smeHwnd)) return true;

    // 2. Direct window under cursor — only try if it has DAD
    //    (medit sample slots, color swatches, etc.)
    HWND under = WindowFromPoint(screenPos);
    if (under && under != smeHwnd) {
        // Safety: only try DAD on small child controls, not main Max windows
        RECT wr; GetWindowRect(under, &wr);
        int w = wr.right - wr.left, h = wr.bottom - wr.top;
        if (w < 300 && h < 300) {
            if (TryDADDropOn(mb, under)) return true;
        }
    }
    return false;
}

// Check if cursor is over an SME node view.
bool IsCursorOverSme()
{
    POINT p{}; GetCursorPos(&p);
    return FindSmeNodeViewWindowAtPoint(p) != nullptr;
}


// ═══════════════════════════════════════════════════════════════
//  Dark Theme
// ═══════════════════════════════════════════════════════════════
namespace Theme
{
    COLORREF bg;
    COLORREF panel;
    COLORREF panelLt;
    COLORREF panelHov;
    COLORREF accent;
    COLORREF text;
    COLORREF textDim;
    COLORREF textBrt;
    COLORREF border;
    COLORREF mapClr;
    COLORREF sceneClr;

    HBRUSH brBg      = nullptr;
    HBRUSH brPanel   = nullptr;
    HBRUSH brPanelLt = nullptr;
    HBRUSH brAccent  = nullptr;
    HFONT  fontUI    = nullptr;
    HFONT  fontBold  = nullptr;

    void Update(bool light)
    {
        if (light) {
            bg       = RGB(215, 218, 222);
            panel    = RGB(225, 228, 232);
            panelLt  = RGB(240, 242, 245);
            panelHov = RGB(250, 250, 250);
            accent   = RGB(150, 155, 165);
            text     = RGB(30, 30, 30);
            textDim  = RGB(100, 100, 100);
            textBrt  = RGB(10, 10, 10);
            border   = RGB(140, 145, 150);
            mapClr   = RGB(80, 100, 180);
            sceneClr = RGB(50, 120, 50);
        } else {
            bg       = RGB(46, 46, 46);
            panel    = RGB(56, 56, 56);
            panelLt  = RGB(68, 68, 68);
            panelHov = RGB(80, 80, 80);
            accent   = RGB(38, 148, 168);
            text     = RGB(220, 220, 220);
            textDim  = RGB(140, 140, 140);
            textBrt  = RGB(255, 255, 255);
            border   = RGB(42, 42, 42);
            mapClr   = RGB(180, 200, 255);
            sceneClr = RGB(100, 200, 100);
        }

        if (brBg) DeleteObject(brBg);
        if (brPanel) DeleteObject(brPanel);
        if (brPanelLt) DeleteObject(brPanelLt);
        if (brAccent) DeleteObject(brAccent);

        brBg      = CreateSolidBrush(bg);
        brPanel   = CreateSolidBrush(panel);
        brPanelLt = CreateSolidBrush(panelLt);
        brAccent  = CreateSolidBrush(accent);
    }

    void Init(bool light)
    {
        Update(light);
        if (!fontUI) {
            fontUI    = CreateFontW(-13, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                            DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            fontBold  = CreateFontW(-15, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET,
                            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                            DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        }
    }
    void Shutdown()
    {
        auto del = [](HGDIOBJ& h) { if (h) { DeleteObject(h); h = nullptr; } };
        del(reinterpret_cast<HGDIOBJ&>(brBg));
        del(reinterpret_cast<HGDIOBJ&>(brPanel));
        del(reinterpret_cast<HGDIOBJ&>(brPanelLt));
        del(reinterpret_cast<HGDIOBJ&>(brAccent));
        del(reinterpret_cast<HGDIOBJ&>(fontUI));
        del(reinterpret_cast<HGDIOBJ&>(fontBold));
    }
}

// ═══════════════════════════════════════════════════════════════
//  Constants
// ═══════════════════════════════════════════════════════════════
constexpr UINT kHotkeyId = 0x514A;
constexpr wchar_t kHotkeyClass[] = L"PowerShaderHotkeyWnd";
constexpr wchar_t kPaletteClass[] = L"PowerShaderPaletteWnd";
constexpr int kSearchId  = 1001;
constexpr int kListId    = 1002;
constexpr int kLinkId    = 1003;
constexpr int kShllId    = 1008;
constexpr int kSceneId   = 1004;
constexpr int kTabMatId  = 1006;
constexpr int kTabMapId  = 1007;
constexpr int kWindowWidth  = 380;
constexpr int kWindowHeight = 540;
constexpr int kHeaderH      = 34;
constexpr UINT_PTR kSearchTimerId = 1;
constexpr UINT kSearchDebounceMs = 14;

enum class TabMode { All, Materials, Maps };
enum class ItemKind { ClassMaterial, ClassMap, SceneMaterial };

struct Item
{
    std::wstring label;
    std::wstring normLabel;   // pre-normalized for scoring
    std::wstring search;
    std::wstring key;
    std::wstring scriptName;
    std::wstring scriptKey;
    std::wstring category;
    ItemKind kind = ItemKind::ClassMaterial;
    ClassDesc* classDesc = nullptr;
    MtlBase* live = nullptr;
};

// ═══════════════════════════════════════════════════════════════
//  Dual favorites
// ═══════════════════════════════════════════════════════════════
struct BrickFav
{
    std::wstring alias;  // normalized class alias
    std::wstring label;  // display label (max 4 chars)
};
constexpr int kBrickBase = 1200;
constexpr int kBrickMax  = 24;  // max persistent brick buttons

// ═══════════════════════════════════════════════════════════════
//  SHLL — Shell Material Preview (pure C++)
// ═══════════════════════════════════════════════════════════════
enum class PBR { None, BaseColor, Roughness, Metalness, Normal, Bump, Emission, Opacity };

static PBR DetectSlotFromName(const wchar_t* n) {
    if (!n || !n[0]) return PBR::None;
    std::wstring s(n); for (auto& c : s) c = towlower(c);
    if (s.find(L"base_color") != s.npos || s.find(L"diffuse") != s.npos || s.find(L"albedo") != s.npos) return PBR::BaseColor;
    if (s.find(L"roughness") != s.npos) return PBR::Roughness;
    if (s.find(L"metalness") != s.npos || s.find(L"metallic") != s.npos) return PBR::Metalness;
    if (s.find(L"normal") != s.npos) return PBR::Normal;
    if (s.find(L"bump") != s.npos || s.find(L"height") != s.npos) return PBR::Bump;
    if (s.find(L"emission") != s.npos || s.find(L"emissive") != s.npos || s.find(L"selfillum") != s.npos) return PBR::Emission;
    if (s.find(L"opacity") != s.npos || s.find(L"alpha") != s.npos || s.find(L"transparency") != s.npos || s.find(L"cutout") != s.npos) return PBR::Opacity;
    return PBR::None;
}

static PBR DetectSlotFromFile(const std::wstring& path) {
    // Extract filename without extension, lowercase
    size_t slash = path.find_last_of(L"\\/");
    size_t dot   = path.find_last_of(L'.');
    std::wstring f = path.substr(slash == path.npos ? 0 : slash + 1,
        dot == path.npos ? path.npos : dot - (slash == path.npos ? 0 : slash + 1));
    for (auto& c : f) c = towlower(c);
    if (f.find(L"basecolor") != f.npos || f.find(L"base_color") != f.npos || f.find(L"diffuse") != f.npos || f.find(L"albedo") != f.npos || f.find(L"_diff") != f.npos || f.find(L"_col") != f.npos) return PBR::BaseColor;
    if (f.find(L"roughness") != f.npos || f.find(L"_rough") != f.npos) return PBR::Roughness;
    if (f.find(L"metallic") != f.npos || f.find(L"metalness") != f.npos || f.find(L"_metal") != f.npos) return PBR::Metalness;
    if (f.find(L"normal") != f.npos || f.find(L"_nrm") != f.npos || f.find(L"_nor") != f.npos) return PBR::Normal;
    if (f.find(L"bump") != f.npos || f.find(L"height") != f.npos || f.find(L"_disp") != f.npos) return PBR::Bump;
    if (f.find(L"emissive") != f.npos || f.find(L"emission") != f.npos || f.find(L"_glow") != f.npos) return PBR::Emission;
    if (f.find(L"opacity") != f.npos || f.find(L"_alpha") != f.npos) return PBR::Opacity;
    return PBR::None;
}

static bool IsImageFile(const wchar_t* path) {
    if (!path || !path[0]) return false;
    const wchar_t* dot = wcsrchr(path, L'.');
    if (!dot) return false;
    std::wstring ext(dot); for (auto& c : ext) c = towlower(c);
    return ext == L".png" || ext == L".jpg" || ext == L".jpeg" || ext == L".tif" ||
           ext == L".tiff" || ext == L".exr" || ext == L".hdr" || ext == L".bmp" ||
           ext == L".tga" || ext == L".dds" || ext == L".psd" || ext == L".tx";
}

// Extract texture file path from any Texmap
static std::wstring ExtractTexPath(Texmap* tex) {
    if (!tex) return {};
    // BitmapTex — direct path
    if (tex->ClassID() == Class_ID(BMTEX_CLASS_ID, 0)) {
        const MCHAR* n = static_cast<BitmapTex*>(tex)->GetMapName();
        if (n && n[0] && IsImageFile(n)) return n;
    }
    // Scan param blocks for TYPE_FILENAME — only accept image files
    for (int b = 0; b < tex->NumParamBlocks(); b++) {
        IParamBlock2* pb = tex->GetParamBlock(b);
        if (!pb) continue;
        for (int i = 0; i < pb->NumParams(); i++) {
            ParamID pid = pb->IndextoID(i);
            const ParamDef& d = pb->GetParamDef(pid);
            if (d.type == TYPE_FILENAME) {
                const MCHAR* fn = pb->GetStr(pid);
                if (fn && fn[0] && IsImageFile(fn)) return fn;
            }
        }
    }
    // Recurse into sub-texmaps
    for (int i = 0; i < tex->NumSubTexmaps(); i++) {
        std::wstring p = ExtractTexPath(tex->GetSubTexmap(i));
        if (!p.empty()) return p;
    }
    return {};
}

// Walk material tree, collect PBR map entries
struct PBRMap { PBR slot; std::wstring path; };

static void CollectPBRMaps(MtlBase* mb, std::vector<PBRMap>& out, std::set<MtlBase*>& visited) {
    if (!mb || visited.count(mb)) return;
    visited.insert(mb);
    // Walk texture slots
    for (int i = 0; i < mb->NumSubTexmaps(); i++) {
        Texmap* sub = mb->GetSubTexmap(i);
        if (!sub) continue;
        std::wstring path = ExtractTexPath(sub);
        if (!path.empty()) {
            MSTR slotName = mb->GetSubTexmapSlotName(i, false);
            PBR slot = DetectSlotFromName(slotName.data());
            if (slot == PBR::None) slot = DetectSlotFromFile(path);
            if (slot != PBR::None) {
                bool dup = false;
                for (auto& e : out) if (e.slot == slot) { dup = true; break; }
                if (!dup) out.push_back({slot, path});
            }
        }
        // Walk deeper into texmap chain
        CollectPBRMaps(sub, out, visited);
    }
    // Walk sub-materials
    if (mb->SuperClassID() == MATERIAL_CLASS_ID) {
        Mtl* mtl = static_cast<Mtl*>(mb);
        for (int i = 0; i < mtl->NumSubMtls(); i++)
            CollectPBRMaps(mtl->GetSubMtl(i), out, visited);
    }
}

// Resize a single bitmap file via Max SDK
static bool ResizeBitmapFile(const std::wstring& src, const std::wstring& dst, int res) {
    BitmapInfo srcBi;
    srcBi.SetName(src.c_str());
    BMMRES status;
    Bitmap* srcBmp = TheManager->Load(&srcBi, &status);
    if (!srcBmp) return false;
    BitmapInfo dstBi;
    dstBi.SetName(dst.c_str());
    dstBi.SetWidth(static_cast<WORD>(res));
    dstBi.SetHeight(static_cast<WORD>(res));
    dstBi.SetType(BMM_TRUE_32);
    Bitmap* dstBmp = TheManager->Create(&dstBi);
    if (!dstBmp) { srcBmp->DeleteThis(); return false; }
    dstBmp->CopyImage(srcBmp, COPY_IMAGE_RESIZE_HI_QUALITY, BMM_Color_64(0,0,0,0));
    bool ok = (dstBmp->OpenOutput(&dstBi) == BMMRES_SUCCESS);
    if (ok) { dstBmp->Write(&dstBi); dstBmp->Close(&dstBi); }
    srcBmp->DeleteThis();
    dstBmp->DeleteThis();
    return ok;
}

// Resize with UDIM support — returns path to use (with <UDIM> preserved)
static std::wstring ResizeTexture(const std::wstring& srcPath, const std::wstring& tmpDir, int res) {
    // Extract parts
    size_t slash = srcPath.find_last_of(L"\\/");
    size_t dot   = srcPath.find_last_of(L'.');
    std::wstring dir  = (slash != srcPath.npos) ? srcPath.substr(0, slash + 1) : L"";
    std::wstring file = srcPath.substr(slash == srcPath.npos ? 0 : slash + 1,
        dot == srcPath.npos ? srcPath.npos : dot - (slash == srcPath.npos ? 0 : slash + 1));
    std::wstring ext  = (dot != srcPath.npos) ? srcPath.substr(dot) : L"";

    // UDIM handling
    if (file.find(L"<UDIM>") != file.npos) {
        std::wstring pattern = dir + file + ext;
        std::wstring glob = pattern;
        size_t pos = glob.find(L"<UDIM>"); glob.replace(pos, 6, L"*");
        WIN32_FIND_DATAW fd;
        HANDLE hFind = FindFirstFileW(glob.c_str(), &fd);
        if (hFind == INVALID_HANDLE_VALUE) return {};
        bool anyOk = false;
        do {
            std::wstring tileSrc = dir + fd.cFileName;
            std::wstring tileDst = tmpDir + L"\\" + fd.cFileName;
            if (ResizeBitmapFile(tileSrc, tileDst, res)) anyOk = true;
        } while (FindNextFileW(hFind, &fd));
        FindClose(hFind);
        return anyOk ? (tmpDir + L"\\" + file + ext) : std::wstring{};
    }

    // Single file
    if (GetFileAttributesW(srcPath.c_str()) == INVALID_FILE_ATTRIBUTES) return {};
    std::wstring outName = file + ext;
    std::wstring dst = tmpDir + L"\\" + outName;
    return ResizeBitmapFile(srcPath, dst, res) ? dst : srcPath;
}

// Set a PB2 param by name
static bool SetPB2Texmap(MtlBase* m, const wchar_t* name, Texmap* tex) {
    for (int b = 0; b < m->NumParamBlocks(); b++) {
        IParamBlock2* pb = m->GetParamBlock(b);
        if (!pb) continue;
        for (int i = 0; i < pb->NumParams(); i++) {
            ParamID pid = pb->IndextoID(i);
            const ParamDef& d = pb->GetParamDef(pid);
            if (d.int_name && _wcsicmp(d.int_name, name) == 0 && d.type == TYPE_TEXMAP)
                { pb->SetValue(pid, 0, tex); return true; }
        }
    }
    return false;
}
static bool SetPB2Bool(MtlBase* m, const wchar_t* name, BOOL val) {
    for (int b = 0; b < m->NumParamBlocks(); b++) {
        IParamBlock2* pb = m->GetParamBlock(b);
        if (!pb) continue;
        for (int i = 0; i < pb->NumParams(); i++) {
            ParamID pid = pb->IndextoID(i);
            const ParamDef& d = pb->GetParamDef(pid);
            if (d.int_name && _wcsicmp(d.int_name, name) == 0 && d.type == TYPE_BOOL)
                { pb->SetValue(pid, 0, val); return true; }
        }
    }
    return false;
}
static bool SetPB2Int(MtlBase* m, const wchar_t* name, int val) {
    for (int b = 0; b < m->NumParamBlocks(); b++) {
        IParamBlock2* pb = m->GetParamBlock(b);
        if (!pb) continue;
        for (int i = 0; i < pb->NumParams(); i++) {
            ParamID pid = pb->IndextoID(i);
            const ParamDef& d = pb->GetParamDef(pid);
            if (d.int_name && _wcsicmp(d.int_name, name) == 0 &&
                (d.type == TYPE_INT || d.type == TYPE_RADIOBTN_INDEX || d.type == TYPE_INDEX))
                { pb->SetValue(pid, 0, val); return true; }
        }
    }
    return false;
}
static bool SetPB2Mtl(MtlBase* m, const wchar_t* name, Mtl* sub) {
    for (int b = 0; b < m->NumParamBlocks(); b++) {
        IParamBlock2* pb = m->GetParamBlock(b);
        if (!pb) continue;
        for (int i = 0; i < pb->NumParams(); i++) {
            ParamID pid = pb->IndextoID(i);
            const ParamDef& d = pb->GetParamDef(pid);
            if (d.int_name && _wcsicmp(d.int_name, name) == 0 && d.type == TYPE_MTL)
                { pb->SetValue(pid, 0, sub); return true; }
        }
    }
    return false;
}

// Find a ClassDesc by superclass + name
static ClassDesc* FindClassByName(SClass_ID sid, const wchar_t* name) {
    SubClassList* list = ClassDirectory::GetInstance().GetClassList(sid);
    if (!list) return nullptr;
    for (int i = list->GetFirst(ACC_PUBLIC); i != -1; i = list->GetNext(ACC_PUBLIC)) {
        ClassEntry& ce = (*list)[i];
        ClassDesc* cd = ce.CD();
        if (cd && _wcsicmp(cd->ClassName(), name) == 0) return cd;
    }
    return nullptr;
}

// Create a texture node for the preview — UberBitmap2 preferred, BitmapTex fallback
static Texmap* CreatePreviewTexmap(const std::wstring& path, ClassDesc* oslCD, const std::wstring& oslShaderPath) {
    // Try UberBitmap2 (OSLMap) first
    if (oslCD) {
        Texmap* osl = static_cast<Texmap*>(oslCD->Create(FALSE));
        if (osl) {
            // Set the OSL shader file
            SetPB2Texmap(osl, L"OSLPath", nullptr); // clear first
            for (int b = 0; b < osl->NumParamBlocks(); b++) {
                IParamBlock2* pb = osl->GetParamBlock(b);
                if (!pb) continue;
                for (int i = 0; i < pb->NumParams(); i++) {
                    ParamID pid = pb->IndextoID(i);
                    const ParamDef& d = pb->GetParamDef(pid);
                    if (d.type == TYPE_FILENAME) {
                        // First filename param = OSL shader path, second = texture
                        const MCHAR* cur = pb->GetStr(pid);
                        if (!cur || !cur[0]) {
                            // Set OSL shader file first time, texture file second time
                            pb->SetValue(pid, 0, const_cast<MCHAR*>(oslShaderPath.c_str()));
                        }
                    }
                }
            }
            // Now set the texture filename — it's typically named "filename"
            for (int b = 0; b < osl->NumParamBlocks(); b++) {
                IParamBlock2* pb = osl->GetParamBlock(b);
                if (!pb) continue;
                for (int i = 0; i < pb->NumParams(); i++) {
                    ParamID pid = pb->IndextoID(i);
                    const ParamDef& d = pb->GetParamDef(pid);
                    if (d.int_name && _wcsicmp(d.int_name, L"filename") == 0) {
                        pb->SetValue(pid, 0, const_cast<MCHAR*>(path.c_str()));
                        return osl;
                    }
                }
            }
            // Fallback: if "filename" param not found, delete and use BitmapTex
            osl->DeleteThis();
        }
    }
    // Fallback to BitmapTex
    BitmapTex* bt = NewDefaultBitmapTex();
    if (bt) bt->SetMapName(path.c_str());
    return bt;
}

static void ExecuteShellCommand(int resolution) {
    Interface* ip = GetCOREInterface();
    if (!ip || ip->GetSelNodeCount() == 0) return;

    // Get temp dir
    MSTR tmpBase = ip->GetDir(APP_TEMP_DIR);
    std::wstring tmpDir = std::wstring(tmpBase.data()) + L"\\PowerShader_SHLL";
    CreateDirectoryW(tmpDir.c_str(), nullptr);

    // Find OSLMap class for UberBitmap2
    ClassDesc* oslCD = FindClassByName(TEXMAP_CLASS_ID, L"OSLMap");
    std::wstring oslPath;
    if (oslCD) {
        MSTR maxRoot = ip->GetDir(APP_MAX_SYS_ROOT_DIR);
        oslPath = std::wstring(maxRoot.data()) + L"\\OSL\\UberBitmap2.osl";
        if (GetFileAttributesW(oslPath.c_str()) == INVALID_FILE_ATTRIBUTES)
            oslCD = nullptr; // OSL file not found, fall back to BitmapTex
    }

    // Find Normal Bump class for normal maps
    ClassDesc* normalBumpCD = FindClassByName(TEXMAP_CLASS_ID, L"Normal Bump");

    std::map<Mtl*, Mtl*> shellCache; // srcMat → shellMat
    int done = 0;

    for (int ni = 0; ni < ip->GetSelNodeCount(); ni++) {
        INode* node = ip->GetSelNode(ni);
        if (!node) continue;
        Mtl* src = node->GetMtl();
        if (!src) continue;
        // Skip if already a Shell
        if (src->ClassID() == Class_ID(BAKE_SHELL_CLASS_ID, 0)) continue;

        // Check cache
        auto cit = shellCache.find(src);
        if (cit != shellCache.end()) { node->SetMtl(cit->second); done++; continue; }

        // Collect PBR maps
        std::vector<PBRMap> maps;
        std::set<MtlBase*> visited;
        CollectPBRMaps(src, maps, visited);
        if (maps.empty()) continue;

        // Create PhysicalMaterial for preview
        Mtl* phys = NewPhysicalMaterial();
        if (!phys) continue;
        MSTR srcName; src->GetClassName(srcName, false);
        std::wstring bn = src->GetName().data();
        phys->SetName(MSTR((L"preview_" + bn).c_str()));

        // Wire each PBR map
        for (auto& mp : maps) {
            std::wstring resized = ResizeTexture(mp.path, tmpDir, resolution);
            if (resized.empty()) continue;

            Texmap* tx = CreatePreviewTexmap(resized, oslCD, oslPath);
            if (!tx) continue;

            switch (mp.slot) {
            case PBR::BaseColor:
                SetPB2Texmap(phys, L"base_color_map", tx);
                SetPB2Bool(phys, L"base_color_map_on", TRUE);
                break;
            case PBR::Roughness:
                SetPB2Texmap(phys, L"roughness_map", tx);
                SetPB2Bool(phys, L"roughness_map_on", TRUE);
                break;
            case PBR::Metalness:
                SetPB2Texmap(phys, L"metalness_map", tx);
                SetPB2Bool(phys, L"metalness_map_on", TRUE);
                break;
            case PBR::Normal:
                if (normalBumpCD) {
                    Texmap* nb = static_cast<Texmap*>(normalBumpCD->Create(FALSE));
                    if (nb) {
                        SetPB2Texmap(nb, L"normal_map", tx);
                        SetPB2Texmap(phys, L"bump_map", nb);
                        SetPB2Bool(phys, L"bump_map_on", TRUE);
                    }
                } else {
                    SetPB2Texmap(phys, L"bump_map", tx);
                    SetPB2Bool(phys, L"bump_map_on", TRUE);
                }
                break;
            case PBR::Bump:
                SetPB2Texmap(phys, L"bump_map", tx);
                SetPB2Bool(phys, L"bump_map_on", TRUE);
                break;
            case PBR::Emission:
                SetPB2Texmap(phys, L"emission_color_map", tx);
                SetPB2Bool(phys, L"emission_color_map_on", TRUE);
                break;
            case PBR::Opacity:
                SetPB2Texmap(phys, L"cutout_map", tx);
                SetPB2Bool(phys, L"cutout_map_on", TRUE);
                break;
            default: break;
            }
        }

        // Create Shell_Material
        Mtl* shell = static_cast<Mtl*>(
            ip->CreateInstance(MATERIAL_CLASS_ID, Class_ID(BAKE_SHELL_CLASS_ID, 0)));
        if (!shell) continue;
        shell->SetName(MSTR((L"shell_" + bn).c_str()));
        SetPB2Mtl(shell, L"originalMaterial", src);
        SetPB2Mtl(shell, L"bakedMaterial", phys);
        SetPB2Int(shell, L"viewportMtlIndex", 1);
        SetPB2Int(shell, L"renderMtlIndex", 0);

        node->SetMtl(shell);
        shellCache[src] = shell;
        done++;
    }

    if (done > 0) ip->RedrawViews(ip->GetTime());
}

static const wchar_t* kLinkScript =
    L"(\n"
    L"if SME.isOpen() do (\n"
    L"local v = SME.GetView (SME.activeView)\n"
    L"local sn = v.GetSelectedNodes()\n"
    L"if sn.count >= 2 do (\n"
    L"local maps = #()\n"
    L"for i = 1 to sn.count do (\n"
    L"local r = try(sn[i].reference)catch(undefined)\n"
    L"if r != undefined do (\n"
    L"local c = classof r\n"
    L"if c == ai_image or c == Bitmaptexture or c == OSLMap or c == RS_Texture do append maps r\n"
    L"))\n"
    L"if maps.count >= 2 do (\n"
    L"local ctrl = bezier_float()\n"
    L"local c1 = classof maps[1]\n"
    L"ctrl.value = case c1 of (\n"
    L"ai_image: maps[1].sscale\n"
    L"Bitmaptexture: maps[1].coords.U_Tiling\n"
    L"RS_Texture: maps[1].scale_x\n"
    L"OSLMap: (try(maps[1].scale)catch(1.0))\n"
    L"default: 1.0)\n"
    L"for m in maps do (\n"
    L"local c = classof m\n"
    L"case c of (\n"
    L"ai_image: (try(m.sscale.controller = ctrl)catch(); try(m.tscale.controller = ctrl)catch())\n"
    L"Bitmaptexture: (try(m.coords.U_Tiling.controller = ctrl)catch(); try(m.coords.V_Tiling.controller = ctrl)catch())\n"
    L"RS_Texture: (try(m.scale_x.controller = ctrl)catch(); try(m.scale_y.controller = ctrl)catch())\n"
    L"OSLMap: (try(m.scale.controller = ctrl)catch())\n"
    L"))))))";

static const wchar_t* kFlushScript =
    L"try(actionMan.executeAction 695602995 \"2\";true)catch(false)";

// ═══════════════════════════════════════════════════════════════
//  Search helpers
// ═══════════════════════════════════════════════════════════════
std::wstring Normalize(const std::wstring& s, bool spaces)
{
    std::wstring out;
    out.reserve(s.size());
    bool lastSpace = true;
    for (wchar_t ch : s)
    {
        wchar_t c = static_cast<wchar_t>(towlower(ch));
        if (iswalnum(c))
        {
            out.push_back(c);
            lastSpace = false;
        }
        else if (spaces && !lastSpace)
        {
            out.push_back(L' ');
            lastSpace = true;
        }
    }
    while (!out.empty() && out.back() == L' ') out.pop_back();
    return out;
}

std::vector<std::wstring> TokenizeQuery(const std::wstring& query)
{
    std::vector<std::wstring> tokens;
    size_t start = 0;
    while (start < query.size())
    {
        while (start < query.size() && query[start] == L' ') ++start;
        if (start >= query.size()) break;
        size_t end = query.find(L' ', start);
        if (end == std::wstring::npos) end = query.size();
        tokens.emplace_back(query.substr(start, end - start));
        start = end + 1;
    }
    return tokens;
}

// Score an item against search tokens. Returns 0 = no match.
int ScoreMatch(const std::wstring& search, const std::wstring& label,
               const std::vector<std::wstring>& tokens)
{
    if (tokens.empty()) return 1;

    int score = 100;
    for (const std::wstring& tok : tokens)
    {
        size_t pos = search.find(tok);
        if (pos == std::wstring::npos) return 0;
        // Word-boundary bonus
        if (pos == 0 || search[pos - 1] == L' ') score += 10;
    }
    // First-token prefix bonus (matches label start)
    if (label.find(tokens[0]) == 0) score += 50;
    // Brevity bonus — shorter names rank higher
    score += std::max(0, 40 - static_cast<int>(label.size()));
    return score;
}

const wchar_t* TagForKind(ItemKind kind)
{
    switch (kind)
    {
    case ItemKind::SceneMaterial: return L"SCENE";
    case ItemKind::ClassMap:      return L"MAP";
    default:                      return L"MAT";
    }
}

// ═══════════════════════════════════════════════════════════════
//  Thin MaxScript bridges (no C++ SDK for these)
// ═══════════════════════════════════════════════════════════════
// SME node creation at deterministic position: selected node + offset,
// else last node + offset, else [0,0].
static const wchar_t* kSmeAtSpawnScript =
    L"("
    L"if not SME.isOpen() do try(SME.Open())catch();"
    L"if SME.isOpen() do ("
    L"local v=sme.getView sme.activeView;"
    L"try("
    L"local p=[0,0];"
    L"try("
    L"local sn=v.GetSelectedNodes();"
    L"if sn!=undefined and sn.count>0 then("
    L"p=sn[1].position+[120,40]"
    L")else if v.GetNumNodes()>0 do("
    L"local ln=v.GetNode (v.GetNumNodes());"
    L"if ln!=undefined do p=ln.position+[120,40]"
    L")"
    L")catch();"
    L"local n=v.CreateNode meditMaterials[activeMeditSlot] p;"
    L"v.SelectNone();try(n.selected=true)catch();"
    L")catch()"
    L")"
    L")";

// Drag: viewport ray-hit → assign material to closest object under cursor.
static const wchar_t* kDragScript =
    L"("
    L"local m=meditMaterials[activeMeditSlot];"
    L"try("
    L"local r=mapScreenToWorldRay mouse.pos;"
    L"local hits=intersectRayScene r;"
    L"local nd=undefined;local best=1e9;"
    L"for h in hits do(local d=distance r.pos h[2].pos;"
    L"if d<best do(best=d;nd=h[1]));"
    L"if nd!=undefined and superclassof m==material do"
    L"(nd.material=m)"
    L")catch()"
    L")";

// ═══════════════════════════════════════════════════════════════
//  DWM dark title bar
// ═══════════════════════════════════════════════════════════════
void EnableDarkTitleBar(HWND hwnd)
{
    HMODULE hDwm = LoadLibraryW(L"dwmapi.dll");
    if (!hDwm) return;
    typedef HRESULT(WINAPI* FN)(HWND, DWORD, LPCVOID, DWORD);
    auto fn = reinterpret_cast<FN>(GetProcAddress(hDwm, "DwmSetWindowAttribute"));
    if (fn) { BOOL v = TRUE; fn(hwnd, 20, &v, sizeof(v)); }
}

// ═══════════════════════════════════════════════════════════════
//  Palette
// ═══════════════════════════════════════════════════════════════
class Palette
{
public:
    static Palette& Get() { static Palette p; return p; }

    bool Init(bool light)
    {
        if (hotkeyWnd_) return true;
        Theme::Init(light);
        INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_STANDARD_CLASSES };
        InitCommonControlsEx(&icc);

        WNDCLASSEXW wc{ sizeof(wc) };
        wc.lpfnWndProc   = HotkeyProc;
        wc.hInstance      = hInstance;
        wc.lpszClassName  = kHotkeyClass;
        RegisterClassExW(&wc);

        wc.lpfnWndProc   = PaletteProc;
        wc.hbrBackground = Theme::brBg;
        wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
        wc.lpszClassName = kPaletteClass;
        RegisterClassExW(&wc);

        hotkeyWnd_ = CreateWindowExW(0, kHotkeyClass, L"", 0, 0, 0, 0, 0,
            HWND_MESSAGE, nullptr, hInstance, this);
        if (!hotkeyWnd_) return false;
        // Class cache is built lazily on first PowerShader open
        return true;
    }

    void Shutdown()
    {
        CancelPendingRebuild();
        if (hotkeyWnd_) { DestroyWindow(hotkeyWnd_); hotkeyWnd_ = nullptr; }
        if (wnd_) { DestroyWindow(wnd_); wnd_ = nullptr; }
        Theme::Shutdown();
    }

    void Toggle()
    {
        if (!EnsureWindow()) return;
        if (IsWindowVisible(wnd_)) Hide(); else Show();
    }

private:
    // ─── Window procedures ──────────────────────────────────────
    static LRESULT CALLBACK HotkeyProc(HWND h, UINT m, WPARAM w, LPARAM l)
    {
        auto* self = reinterpret_cast<Palette*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (m == WM_NCCREATE)
        {
            self = static_cast<Palette*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
            SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            return TRUE;
        }
        if (self && m == WM_HOTKEY && w == kHotkeyId) { self->Toggle(); return 0; }
        return DefWindowProcW(h, m, w, l);
    }

    static LRESULT CALLBACK PaletteProc(HWND h, UINT m, WPARAM w, LPARAM l)
    {
        auto* self = reinterpret_cast<Palette*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (m == WM_NCCREATE)
        {
            self = static_cast<Palette*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
            SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            return TRUE;
        }
        if (!self) return DefWindowProcW(h, m, w, l);

        switch (m)
        {
        case WM_CREATE:
            self->OnCreate(h);
            return 0;

        case WM_COMMAND:
            self->OnCommand(LOWORD(w), HIWORD(w));
            return 0;

        case WM_TIMER:
            self->OnTimer(static_cast<UINT_PTR>(w));
            return 0;

        case WM_ACTIVATE:
            if (LOWORD(w) == WA_INACTIVE && !self->dragging_) self->Hide();
            return 0;

        case WM_CLOSE:
            self->Hide();
            return 0;

        // ─── Custom header + border ─────────────────────────────
        case WM_ERASEBKGND:
        {
            HDC hdc = reinterpret_cast<HDC>(w);
            RECT rc; GetClientRect(h, &rc);
            FillRect(hdc, &rc, Theme::brBg);
            // Border
            HPEN bp = CreatePen(PS_SOLID, 1, Theme::border);
            SelectObject(hdc, bp); SelectObject(hdc, GetStockObject(NULL_BRUSH));
            Rectangle(hdc, 0, 0, rc.right, rc.bottom); DeleteObject(bp);
            // Title
            SetBkMode(hdc, TRANSPARENT);
            HFONT oldF = static_cast<HFONT>(SelectObject(hdc, Theme::fontBold));
            SetTextColor(hdc, Theme::accent);
            TextOutW(hdc, 10, 10, L"flowstate.", 10);
            // Close button
            if (self->hoverClose_) {
                HBRUSH hov = CreateSolidBrush(RGB(200, 60, 60));
                FillRect(hdc, &self->closeRect_, hov); DeleteObject(hov);
            }
            SetTextColor(hdc, Theme::text);
            DrawTextW(hdc, L"\u00D7", 1, &self->closeRect_,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            // Separator
            HPEN sep = CreatePen(PS_SOLID, 1, Theme::border);
            SelectObject(hdc, sep);
            MoveToEx(hdc, 8, kHeaderH - 4, nullptr);
            LineTo(hdc, rc.right - 8, kHeaderH - 4);
            DeleteObject(sep);
            SelectObject(hdc, oldF);
            return 1;
        }

        case WM_NCHITTEST:
        {
            POINT pt = { GET_X_LPARAM(l), GET_Y_LPARAM(l) };
            ScreenToClient(h, &pt);
            if (PtInRect(&self->closeRect_, pt)) return HTCLIENT;
            if (pt.y < kHeaderH) return HTCAPTION;
            return HTCLIENT;
        }

        case WM_LBUTTONDOWN:
        {
            POINT pt = { GET_X_LPARAM(l), GET_Y_LPARAM(l) };
            if (PtInRect(&self->closeRect_, pt)) { self->Hide(); return 0; }
            break;
        }

        case WM_LBUTTONUP:
            // Finish rename on click outside
            if (self->renameEdit_) self->FinishRename();
            // Brick drag completion
            if (self->brickDragging_ && self->brickDragId_ >= kBrickBase) {
                ReleaseCapture();
                int bi = self->brickDragId_ - kBrickBase;
                self->brickDragging_ = false;
                self->brickDragId_ = -1;
                if (bi >= 0 && bi < static_cast<int>(self->brickFavs_.size()))
                    self->ActivateAlias(self->brickFavs_[bi].alias, true);
                return 0;
            }
            break;

        case WM_PARENTNOTIFY:
        {
            // Middle-click on brick button → remove it
            if (LOWORD(w) == WM_MBUTTONDOWN) {
                POINT cp = { GET_X_LPARAM(l), GET_Y_LPARAM(l) };
                HWND child = ChildWindowFromPoint(h, cp);
                int cid = child ? GetDlgCtrlID(child) : 0;
                if (cid >= kBrickBase && cid < kBrickBase + kBrickMax) {
                    self->RemoveBrickFav(cid - kBrickBase);
                    return 0;
                }
            }
            break;
        }

        case WM_CONTEXTMENU:
        {
            HWND target = reinterpret_cast<HWND>(w);
            int cid = target ? GetDlgCtrlID(target) : 0;
            // Right-click SHLL → cycle resolution
            if (cid == kShllId) {
                static const int kRes[] = {128, 256, 512, 1024};
                int cur = 0;
                for (int i = 0; i < 4; i++) if (kRes[i] == self->shllRes_) { cur = i; break; }
                self->shllRes_ = kRes[(cur + 1) % 4];
                std::wstring lbl = L"SHLL " + std::to_wstring(self->shllRes_);
                SetWindowTextW(self->shll_, lbl.c_str());
                InvalidateRect(self->shll_, nullptr, FALSE);
                return 0;
            }
            // Right-click on brick button → rename
            if (cid >= kBrickBase && cid < kBrickBase + kBrickMax) {
                self->RenameBrickFav(cid - kBrickBase);
                return 0;
            }
            break;
        }

        case WM_MOUSEMOVE:
        {
            POINT pt = { GET_X_LPARAM(l), GET_Y_LPARAM(l) };
            bool hover = PtInRect(&self->closeRect_, pt) != 0;
            if (hover != self->hoverClose_) {
                self->hoverClose_ = hover;
                InvalidateRect(h, &self->closeRect_, TRUE);
            }
            if (!self->trackingMouse_) {
                TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, h, 0 };
                TrackMouseEvent(&tme);
                self->trackingMouse_ = true;
            }
            break;
        }

        case WM_MOUSELEAVE:
            if (self->hoverClose_) {
                self->hoverClose_ = false;
                InvalidateRect(h, &self->closeRect_, TRUE);
            }
            self->trackingMouse_ = false;
            break;

        // ─── Dark theme color handlers ──────────────────────────
        case WM_CTLCOLOREDIT:
        {
            HDC hdc = reinterpret_cast<HDC>(w);
            SetTextColor(hdc, Theme::textBrt);
            SetBkColor(hdc, Theme::panel);
            return reinterpret_cast<LRESULT>(Theme::brPanel);
        }
        case WM_CTLCOLORSTATIC:
        {
            HDC hdc = reinterpret_cast<HDC>(w);
            SetTextColor(hdc, Theme::textDim);
            SetBkColor(hdc, Theme::bg);
            return reinterpret_cast<LRESULT>(Theme::brBg);
        }
        case WM_CTLCOLORLISTBOX:
        {
            HDC hdc = reinterpret_cast<HDC>(w);
            SetTextColor(hdc, Theme::text);
            SetBkColor(hdc, Theme::bg);
            return reinterpret_cast<LRESULT>(Theme::brBg);
        }
        case WM_CTLCOLORBTN:
        {
            HDC hdc = reinterpret_cast<HDC>(w);
            SetTextColor(hdc, Theme::text);
            SetBkColor(hdc, Theme::bg);
            return reinterpret_cast<LRESULT>(Theme::brBg);
        }

        // ─── Owner-draw handlers ────────────────────────────────
        case WM_MEASUREITEM:
        {
            auto* mis = reinterpret_cast<MEASUREITEMSTRUCT*>(l);
            mis->itemHeight = 24;
            return TRUE;
        }
        case WM_DRAWITEM:
        {
            auto* dis = reinterpret_cast<DRAWITEMSTRUCT*>(l);
            if (dis->CtlID == kListId)
                { self->DrawListItem(dis); return TRUE; }
            if (dis->CtlID == kTabMatId || dis->CtlID == kTabMapId ||
                dis->CtlID == kLinkId || dis->CtlID == kShllId ||
                dis->CtlID == kSceneId ||
                (dis->CtlID >= kBrickBase && dis->CtlID < kBrickBase + kBrickMax))
                { self->DrawButton(dis); return TRUE; }
            break;
        }
        }
        return DefWindowProcW(h, m, w, l);
    }

    static LRESULT CALLBACK EditProc(HWND h, UINT m, WPARAM w, LPARAM l,
                                     UINT_PTR, DWORD_PTR ref)
    {
        auto* self = reinterpret_cast<Palette*>(ref);
        if (m == WM_KEYDOWN)
        {
            if (w == VK_RETURN) { self->ActivateCurrent(false); return 0; }
            if (w == VK_DOWN)   { self->MoveSelection(1); return 0; }
            if (w == VK_UP)     { self->MoveSelection(-1); return 0; }
            if (w == VK_ESCAPE) { self->Hide(); return 0; }
        }
        return DefSubclassProc(h, m, w, l);
    }

    static LRESULT CALLBACK ListProc(HWND h, UINT m, WPARAM w, LPARAM l,
                                     UINT_PTR, DWORD_PTR ref)
    {
        auto* self = reinterpret_cast<Palette*>(ref);
        switch (m)
        {
        case WM_LBUTTONDOWN:
        {
            LRESULT hit = SendMessageW(h, LB_ITEMFROMPOINT, 0, l);
            if (HIWORD(hit) == 0)
            {
                self->dragIndex_ = LOWORD(hit);
                self->dragging_ = false;
                GetCursorPos(&self->dragStart_);
                SetCapture(h);
            }
            break;
        }
        case WM_MOUSEMOVE:
            if (GetCapture() == h && (w & MK_LBUTTON))
            {
                POINT p{}; GetCursorPos(&p);
                if (std::abs(p.x - self->dragStart_.x) > 6 ||
                    std::abs(p.y - self->dragStart_.y) > 6)
                    self->dragging_ = true;
            }
            break;
        case WM_LBUTTONUP:
            if (GetCapture() == h) ReleaseCapture();
            if (self->dragging_)
            {
                POINT p{}; GetCursorPos(&p);
                HWND under = WindowFromPoint(p);
                if (!under || (under != self->wnd_ && !IsChild(self->wnd_, under)))
                    self->ActivateByIndex(self->dragIndex_, true);
            }
            self->dragging_ = false;
            self->dragIndex_ = -1;
            break;
        case WM_RBUTTONDOWN:
        {
            // Right-click = file-local pin (toggle at start of list)
            LRESULT hit = SendMessageW(h, LB_ITEMFROMPOINT, 0, l);
            if (HIWORD(hit) == 0) self->ToggleFilePin(LOWORD(hit));
            return 0;
        }
        case WM_MBUTTONDOWN:
        {
            // Middle-click = persistent brick favorite
            LRESULT hit = SendMessageW(h, LB_ITEMFROMPOINT, 0, l);
            if (HIWORD(hit) == 0) self->ToggleBrickFav(LOWORD(hit));
            return 0;
        }
        }
        return DefSubclassProc(h, m, w, l);
    }

    static LRESULT CALLBACK BrickBtnProc(HWND h, UINT m, WPARAM w, LPARAM l,
                                         UINT_PTR, DWORD_PTR ref)
    {
        auto* self = reinterpret_cast<Palette*>(ref);
        switch (m)
        {
        case WM_LBUTTONDOWN:
            self->brickDragId_ = GetDlgCtrlID(h);
            self->brickDragging_ = false;
            GetCursorPos(&self->dragStart_);
            break;
        case WM_MOUSEMOVE:
            if (self->brickDragId_ >= 0 && (w & MK_LBUTTON) && !self->brickDragging_)
            {
                POINT p{}; GetCursorPos(&p);
                if (std::abs(p.x - self->dragStart_.x) > 6 ||
                    std::abs(p.y - self->dragStart_.y) > 6)
                {
                    self->brickDragging_ = true;
                    ReleaseCapture();
                    SetCapture(self->wnd_);
                    return 0;
                }
            }
            break;
        case WM_LBUTTONUP:
            self->brickDragId_ = -1;
            break;
        }
        return DefSubclassProc(h, m, w, l);
    }

    // ─── Window management ──────────────────────────────────────
    bool EnsureWindow()
    {
        if (wnd_) return true;
        wnd_ = CreateWindowExW(
            WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_LAYERED, kPaletteClass,
            nullptr,
            WS_POPUP,
            CW_USEDEFAULT, CW_USEDEFAULT, kWindowWidth, kWindowHeight,
            GetCOREInterface() ? GetCOREInterface()->GetMAXHWnd() : nullptr,
            nullptr, hInstance, this);
        return wnd_ != nullptr;
    }

    // ─── UI creation ────────────────────────────────────────────
    void OnCreate(HWND h)
    {
        wnd_ = h;
        const int pad = 8;
        RECT cr; GetClientRect(h, &cr);
        const int cw = cr.right - 2 * pad;
        closeRect_ = { cr.right - pad - 18, pad, cr.right - pad, pad + 18 };
        int y = kHeaderH;

        // Search box
        edit_ = CreateWindowExW(0, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            pad, y, cw, 24, h,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSearchId)),
            hInstance, nullptr);
        SendMessageW(edit_, WM_SETFONT, reinterpret_cast<WPARAM>(Theme::fontBold), TRUE);
        SendMessageW(edit_, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Search shaders..."));
        SetWindowSubclass(edit_, EditProc, 1, reinterpret_cast<DWORD_PTR>(this));
        y += 28;

        // Filter toggles: Shaders | Maps (clicking active one = show ALL)
        const int tabGap = 3;
        int tabW = (cw - tabGap) / 2;
        CreateWindowExW(0, L"BUTTON", L"Shaders",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            pad, y, tabW, 22, h,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kTabMatId)), hInstance, nullptr);
        CreateWindowExW(0, L"BUTTON", L"Maps",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            pad + tabW + tabGap, y, cw - tabW - tabGap, 22, h,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kTabMapId)), hInstance, nullptr);
        y += 26;

        // Command row: LINK | SHLL | Scene
        const int gap = 3;
        int btnW3 = (cw - 2 * gap) / 3;
        link_ = CreateWindowExW(0, L"BUTTON", L"LINK",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            pad, y, btnW3, 22, h,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kLinkId)), hInstance, nullptr);
        shll_ = CreateWindowExW(0, L"BUTTON", L"SHLL",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            pad + btnW3 + gap, y, btnW3, 22, h,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kShllId)), hInstance, nullptr);
        scene_ = CreateWindowExW(0, L"BUTTON", L"Scene",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            pad + 2 * (btnW3 + gap), y, cw - 2 * (btnW3 + gap), 22, h,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSceneId)), hInstance, nullptr);
        y += 26;

        // Results list (owner-drawn)
        listBaseY_ = y;
        int listH = cr.bottom - y - 26;
        list_ = CreateWindowExW(0, L"LISTBOX", L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY |
            LBS_NOINTEGRALHEIGHT | LBS_OWNERDRAWFIXED | LBS_NODATA,
            pad, y, cw, listH, h,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kListId)), hInstance, nullptr);
        SetWindowSubclass(list_, ListProc, 1, reinterpret_cast<DWORD_PTR>(this));

        // Status bar
        status_ = CreateWindowExW(0, L"STATIC", L"Ready r4",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            pad, cr.bottom - 22, cw, 18, h, nullptr, hInstance, nullptr);
        SendMessageW(status_, WM_SETFONT, reinterpret_cast<WPARAM>(Theme::fontUI), TRUE);

        // Dark scrollbar on list
        HMODULE hUx = LoadLibraryW(L"uxtheme.dll");
        if (hUx)
        {
            typedef HRESULT(WINAPI* SWTF)(HWND, LPCWSTR, LPCWSTR);
            auto swt = reinterpret_cast<SWTF>(GetProcAddress(hUx, "SetWindowTheme"));
            if (swt) swt(list_, L"DarkMode_Explorer", nullptr);
        }
    }

    // ─── Drawing ────────────────────────────────────────────────
    void DrawListItem(DRAWITEMSTRUCT* dis)
    {
        if (dis->itemID == static_cast<UINT>(-1)) return;
        int fi = static_cast<int>(dis->itemID);
        if (!activeItems_ || fi < 0 || fi >= static_cast<int>(filtered_.size())) return;
        int si = filtered_[static_cast<size_t>(fi)];
        if (si < 0 || si >= static_cast<int>(activeItems_->size())) return;
        const Item& item = (*activeItems_)[static_cast<size_t>(si)];

        bool sel = (dis->itemState & ODS_SELECTED) != 0;
        FillRect(dis->hDC, &dis->rcItem, sel ? Theme::brAccent : Theme::brBg);

        // Name color by type
        COLORREF tc = sel ? Theme::textBrt : Theme::text;
        if (!sel)
        {
            if (item.kind == ItemKind::ClassMap) tc = Theme::mapClr;
            if (item.kind == ItemKind::SceneMaterial) tc = Theme::sceneClr;
        }

        SetBkMode(dis->hDC, TRANSPARENT);
        HFONT oldF = static_cast<HFONT>(SelectObject(dis->hDC, Theme::fontUI));

        // Right side: category · TAG
        std::wstring info = item.category;
        if (!info.empty()) info += L" \u00B7 ";
        info += TagForKind(item.kind);
        RECT rr = dis->rcItem;
        rr.right -= 6;
        SetTextColor(dis->hDC, sel ? Theme::textBrt : Theme::textDim);
        DrawTextW(dis->hDC, info.c_str(), -1, &rr,
            DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

        // Measure right text to clip left
        SIZE infoSz{};
        GetTextExtentPoint32W(dis->hDC, info.c_str(), static_cast<int>(info.size()), &infoSz);

        // Left side: pin indicator + item name
        bool isPinned = std::find(filePins_.begin(), filePins_.end(), item.key) != filePins_.end();
        RECT lr = dis->rcItem;
        lr.left += 8;
        if (isPinned) {
            SetTextColor(dis->hDC, Theme::accent);
            DrawTextW(dis->hDC, L"\u25C6 ", 2, &lr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            lr.left += 14;
        }
        lr.right -= infoSz.cx + 12;
        SetTextColor(dis->hDC, tc);
        DrawTextW(dis->hDC, item.label.c_str(), -1, &lr,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        SelectObject(dis->hDC, oldF);

        if (!sel)
        {
            HPEN pen = CreatePen(PS_SOLID, 1, Theme::border);
            HPEN oldP = static_cast<HPEN>(SelectObject(dis->hDC, pen));
            MoveToEx(dis->hDC, dis->rcItem.left, dis->rcItem.bottom - 1, nullptr);
            LineTo(dis->hDC, dis->rcItem.right, dis->rcItem.bottom - 1);
            SelectObject(dis->hDC, oldP);
            DeleteObject(pen);
        }
    }

    void DrawButton(DRAWITEMSTRUCT* dis)
    {
        int id = static_cast<int>(dis->CtlID);
        bool active = false;

        // Determine active state based on control type
        if (id == kTabMatId)      active = (tab_ == TabMode::Materials);
        else if (id == kTabMapId) active = (tab_ == TabMode::Maps);
        else if (id == kSceneId)  active = sceneOnly_;
        else                      active = (dis->itemState & ODS_SELECTED) != 0;

        COLORREF bgc = active ? Theme::accent : Theme::panelLt;
        HBRUSH br = CreateSolidBrush(bgc);
        FillRect(dis->hDC, &dis->rcItem, br);
        DeleteObject(br);

        HPEN pen = CreatePen(PS_SOLID, 1, Theme::border);
        HPEN oldP = static_cast<HPEN>(SelectObject(dis->hDC, pen));
        HBRUSH oldBr = static_cast<HBRUSH>(
            SelectObject(dis->hDC, GetStockObject(NULL_BRUSH)));
        Rectangle(dis->hDC, dis->rcItem.left, dis->rcItem.top,
                  dis->rcItem.right, dis->rcItem.bottom);
        SelectObject(dis->hDC, oldBr);
        SelectObject(dis->hDC, oldP);
        DeleteObject(pen);

        wchar_t buf[32] = {};
        GetWindowTextW(dis->hwndItem, buf, 32);
        SetBkMode(dis->hDC, TRANSPARENT);
        SetTextColor(dis->hDC, active ? Theme::textBrt : Theme::text);
        HFONT old = static_cast<HFONT>(SelectObject(dis->hDC, Theme::fontUI));
        DrawTextW(dis->hDC, buf, -1, &dis->rcItem,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(dis->hDC, old);
    }

    // ─── Show / Hide ────────────────────────────────────────────
    void Show()
    {
        CancelPendingRebuild();
        SetWindowTextW(edit_, L"");
        LoadFilePins();
        LoadBrickFavs();
        EnsureClassCache();
        if (IsSceneOnly()) RefreshSceneCache();
        Rebuild(true);
        RebuildBrickUI();
        POINT p{}; GetCursorPos(&p);
        RECT wa{}; SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
        int x = std::clamp(static_cast<long>(p.x - kWindowWidth / 2),
                           wa.left, wa.right - static_cast<long>(kWindowWidth));
        int y = std::clamp(static_cast<long>(p.y + 20),
                           wa.top, wa.bottom - static_cast<long>(kWindowHeight));
        int startY = y + 15;
        SetLayeredWindowAttributes(wnd_, 0, 0, LWA_ALPHA);
        ShowWindow(wnd_, SW_SHOW);
        SetWindowPos(wnd_, HWND_TOPMOST, x, startY, kWindowWidth, kWindowHeight, 0);
        // Fade & slide in — cubic ease-out, 80ms
        DWORD t0 = GetTickCount();
        for (;;) {
            float t = (float)(GetTickCount() - t0) / 80.0f;
            if (t >= 1.0f) { 
                SetLayeredWindowAttributes(wnd_, 0, 255, LWA_ALPHA); 
                SetWindowPos(wnd_, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
                break; 
            }
            float ease = 1.0f - (1.0f-t)*(1.0f-t)*(1.0f-t);
            BYTE a = (BYTE)(255.0f * ease);
            int curY = startY - (int)(15.0f * ease);
            SetLayeredWindowAttributes(wnd_, 0, a, LWA_ALPHA);
            SetWindowPos(wnd_, nullptr, x, curY, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            MSG msg; while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessage(&msg); }
        }
        SetActiveWindow(wnd_);
        SetForegroundWindow(wnd_);
        SetFocus(edit_);
        SendMessageW(edit_, EM_SETSEL, 0, -1);
        // Stop Max from stealing keyboard input (M, S, P etc. are Max shortcuts)
        DisableAccelerators();
    }

    void Hide()
    {
        CancelPendingRebuild();
        // Fade & slide out — cubic ease-in, 50ms
        if (IsWindowVisible(wnd_)) {
            RECT r; GetWindowRect(wnd_, &r);
            int x = r.left, y = r.top;
            DWORD t0 = GetTickCount();
            for (;;) {
                float t = (float)(GetTickCount() - t0) / 50.0f;
                if (t >= 1.0f) break;
                float ease = t * t * t;
                BYTE a = (BYTE)(255.0f * (1.0f - ease));
                int curY = y + (int)(10.0f * ease);
                SetLayeredWindowAttributes(wnd_, 0, a, LWA_ALPHA);
                SetWindowPos(wnd_, nullptr, x, curY, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
                MSG msg; while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessage(&msg); }
            }
        }
        ShowWindow(wnd_, SW_HIDE);
        SetLayeredWindowAttributes(wnd_, 0, 255, LWA_ALPHA);
        dragging_ = false;
        dragIndex_ = -1;
        // Restore Max keyboard shortcuts
        EnableAccelerators();
    }

    // ─── Commands ───────────────────────────────────────────────
    void OnCommand(int id, int code)
    {
        if (id == kSearchId && code == EN_CHANGE) { ScheduleRebuild(); return; }
        if (id == kLinkId) {
            ExecuteMAXScriptScript(kLinkScript, MAXScript::ScriptSource::Dynamic);
            return;
        }
        if (id == kShllId) {
            ExecuteShellCommand(shllRes_);
            return;
        }
        if (id == kSceneId)
        {
            sceneOnly_ = !sceneOnly_;
            InvalidateRect(scene_, nullptr, FALSE);
            CancelPendingRebuild();
            if (sceneOnly_) RefreshSceneCache();
            Rebuild(true);
            return;
        }
        if (id == kTabMatId || id == kTabMapId)
        {
            // Toggle: clicking active tab deactivates it (shows ALL)
            TabMode clicked = (id == kTabMatId) ? TabMode::Materials : TabMode::Maps;
            tab_ = (tab_ == clicked) ? TabMode::All : clicked;
            for (int tid : {kTabMatId, kTabMapId})
                if (HWND tw = GetDlgItem(wnd_, tid)) InvalidateRect(tw, nullptr, FALSE);
            CancelPendingRebuild();
            Rebuild(true);
            return;
        }
        if (id == kListId && code == LBN_DBLCLK)
            { ActivateCurrent(false); return; }
        // Brick favorite click → activate that alias
        if (id >= kBrickBase && id < kBrickBase + kBrickMax) {
            int bi = id - kBrickBase;
            if (bi >= 0 && bi < static_cast<int>(brickFavs_.size()))
                ActivateAlias(brickFavs_[bi].alias);
            return;
        }
    }

    void OnTimer(UINT_PTR id)
    {
        if (id != kSearchTimerId) return;
        CancelPendingRebuild();
        Rebuild(false);
    }

    // ─── Data scanning ─────────────────────────────────────────
    bool IsSceneOnly() const
    {
        return sceneOnly_;
    }

    std::wstring ReadNormalizedQuery() const
    {
        int len = GetWindowTextLengthW(edit_);
        std::wstring q(static_cast<size_t>(len + 1), L'\0');
        GetWindowTextW(edit_, q.data(), len + 1);
        q.resize(static_cast<size_t>(len));
        return Normalize(q, true);
    }

    void ScheduleRebuild()
    {
        if (!wnd_ || rebuildPending_) return;
        rebuildPending_ = true;
        SetTimer(wnd_, kSearchTimerId, kSearchDebounceMs, nullptr);
    }

    void CancelPendingRebuild()
    {
        if (!wnd_ || !rebuildPending_) return;
        KillTimer(wnd_, kSearchTimerId);
        rebuildPending_ = false;
    }

    void EnsureClassCache()
    {
        if (classCacheReady_) return;

        classItems_.clear();
        classItems_.reserve(1024);
        AddClassList(MATERIAL_CLASS_ID, ItemKind::ClassMaterial, classItems_);
        AddClassList(TEXMAP_CLASS_ID, ItemKind::ClassMap, classItems_);
        std::sort(classItems_.begin(), classItems_.end(),
            [](const Item& a, const Item& b) { return a.label < b.label; });

        classCacheReady_ = true;
    }

    void RefreshSceneCache()
    {
        sceneItems_.clear();
        Interface* ip = GetCOREInterface();
        if (ip && ip->GetSceneMtls())
        {
            MtlBaseLib* lib = ip->GetSceneMtls();
            sceneItems_.reserve(lib->Count());
            for (int i = 0; i < lib->Count(); ++i)
            {
                MtlBase* m = (*lib)[i];
                if (!m) continue;
                Item item;
                MSTR className = m->ClassName();
                item.label = m->GetName().Length()
                    ? std::wstring(m->GetName().data())
                    : std::wstring(className.data());
                item.normLabel = Normalize(item.label, true);
                item.search = Normalize(
                    item.label + L" " + std::wstring(className.data()), true);
                item.key = Normalize(item.label, false);
                item.kind = ItemKind::SceneMaterial;
                item.category = std::wstring(className.data());
                item.live = m;
                sceneItems_.push_back(std::move(item));
            }
        }
        std::sort(sceneItems_.begin(), sceneItems_.end(),
            [](const Item& a, const Item& b) { return a.label < b.label; });
        sceneCacheReady_ = true;
    }

    void AddClassList(SClass_ID sid, ItemKind kind, std::vector<Item>& out)
    {
        SubClassList* list = ClassDirectory::GetInstance().GetClassList(sid);
        if (!list) return;
        using CIDPair = std::pair<ULONG, ULONG>;
        std::set<CIDPair> seen;

        for (int i = list->GetFirst(ACC_PUBLIC); i != -1;
             i = list->GetNext(ACC_PUBLIC))
        {
            ClassEntry& ce = (*list)[i];
            ClassDesc* cd = ce.FullCD();
            if (!cd) continue;

            const Class_ID classId = ce.ClassID();
            if (!seen.insert({classId.PartA(), classId.PartB()}).second)
                continue;

            const MCHAR* nonLocalized = cd->NonLocalizedClassName();
            const MCHAR* className    = cd->ClassName();
            const MCHAR* internalName = cd->InternalName();
            const MCHAR* categoryName = cd->Category();
            std::wstring name = (nonLocalized && nonLocalized[0])
                ? std::wstring(nonLocalized)
                : std::wstring(className ? className : L"");
            if (name.empty() && internalName && internalName[0])
                name = std::wstring(internalName);
            std::wstring key = Normalize(name, false);
            if (key.empty()) continue;
            Item item;
            item.label      = name;
            item.normLabel  = Normalize(name, true);
            item.search     = Normalize(
                name + L" " +
                std::wstring(className ? className : L"") + L" " +
                std::wstring(internalName ? internalName : L"") + L" " +
                std::wstring(categoryName ? categoryName : L""), true);
            item.key        = key;
            item.scriptName = (internalName && internalName[0])
                ? std::wstring(internalName) : L"";
            item.scriptKey  = Normalize(item.scriptName, false);
            item.kind       = kind;
            item.category   = (categoryName && categoryName[0])
                ? std::wstring(categoryName) : L"";
            item.classDesc  = cd;
            out.push_back(std::move(item));
        }
    }

    // ─── List rebuild ───────────────────────────────────────────
    void Rebuild(bool forceFull)
    {
        const bool sceneOnly = IsSceneOnly();
        EnsureClassCache();
        if (sceneOnly && !sceneCacheReady_) RefreshSceneCache();

        const std::vector<Item>& source = sceneOnly ? sceneItems_ : classItems_;
        activeItems_ = &source;

        const std::wstring q = ReadNormalizedQuery();
        const std::wstring normQ = Normalize(q, true);
        const std::vector<std::wstring> tokens = TokenizeQuery(normQ);

        auto passesTab = [&](const Item& item) -> bool
        {
            bool isScene = item.kind == ItemKind::SceneMaterial;
            if (sceneOnly != isScene) return false;
            if (!sceneOnly)
            {
                if (tab_ == TabMode::Materials &&
                    item.kind != ItemKind::ClassMaterial) return false;
                if (tab_ == TabMode::Maps &&
                    item.kind != ItemKind::ClassMap) return false;
            }
            return true;
        };

        struct Scored { int idx; int score; };
        std::vector<Scored> scored;
        scored.reserve(source.size());

        for (size_t i = 0; i < source.size(); ++i)
        {
            const Item& item = source[i];
            if (!passesTab(item)) continue;
            int s = ScoreMatch(item.search, item.normLabel, tokens);
            if (s > 0) scored.push_back({static_cast<int>(i), s});
        }

        // Sort by score descending when searching, alphabetical otherwise
        if (!tokens.empty())
            std::sort(scored.begin(), scored.end(),
                [](const Scored& a, const Scored& b) { return a.score > b.score; });

        filtered_.clear();
        filtered_.reserve(scored.size());

        // File-pinned items go first (only when not actively searching)
        if (tokens.empty() && !filePins_.empty()) {
            std::set<int> pinIndices;
            for (const auto& pin : filePins_) {
                for (size_t i = 0; i < source.size(); i++) {
                    if (source[i].key == pin) {
                        filtered_.push_back(static_cast<int>(i));
                        pinIndices.insert(static_cast<int>(i));
                        break;
                    }
                }
            }
            for (const auto& s : scored)
                if (pinIndices.find(s.idx) == pinIndices.end())
                    filtered_.push_back(s.idx);
        } else {
            for (const auto& s : scored) filtered_.push_back(s.idx);
        }

        SendMessageW(list_, WM_SETREDRAW, FALSE, 0);
        SendMessageW(list_, LB_RESETCONTENT, 0, 0);
        SendMessageW(list_, LB_SETCOUNT, static_cast<WPARAM>(filtered_.size()), 0);
        if (!filtered_.empty()) SendMessageW(list_, LB_SETCURSEL, 0, 0);
        SendMessageW(list_, WM_SETREDRAW, TRUE, 0);
        RedrawWindow(list_, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);

        lastQuery_ = normQ;
        lastTab_ = tab_;
        lastSceneOnly_ = sceneOnly;

        SetStatus(std::to_wstring(filtered_.size()) + L" items" +
            (normQ.empty() ? L"" : (L"  |  " + normQ)));
    }

    // ─── Activation (C++ API core) ──────────────────────────────
    void ActivateAlias(const std::wstring& alias, bool drag = false)
    {
        EnsureClassCache();
        std::wstring key = Normalize(alias, false);
        for (const Item& item : classItems_)
            if (!item.live &&
                (item.key == key || item.scriptKey == key))
                { Activate(item, drag); return; }

        // Retry once after forcing a fresh cache build.
        if (!forcedAliasRetry_)
        {
            forcedAliasRetry_ = true;
            classCacheReady_ = false;
            EnsureClassCache();
            for (const Item& item : classItems_)
                if (!item.live &&
                    (item.key == key || item.scriptKey == key))
                    { Activate(item, drag); return; }
        }

        SetStatus(L"Class not available.");
    }

    void ActivateCurrent(bool drag)
    {
        if (rebuildPending_)
        {
            CancelPendingRebuild();
            Rebuild(false);
        }
        ActivateByIndex(
            static_cast<int>(SendMessageW(list_, LB_GETCURSEL, 0, 0)), drag);
    }

    void ActivateByIndex(int idx, bool drag)
    {
        if (!activeItems_) return;
        if (idx < 0 || idx >= static_cast<int>(filtered_.size())) return;
        int sourceIdx = filtered_[static_cast<size_t>(idx)];
        if (sourceIdx < 0 || sourceIdx >= static_cast<int>(activeItems_->size())) return;
        Activate((*activeItems_)[static_cast<size_t>(sourceIdx)], drag);
    }

    void Activate(const Item& item, bool drag)
    {
        Interface* ip = GetCOREInterface();
        if (!ip) { SetStatus(L"No interface."); return; }

        // ── Create or reuse material/map instance ───────────────
        MtlBase* mb = item.live;
        if (!mb && item.classDesc)
            mb = static_cast<MtlBase*>(item.classDesc->Create(FALSE));
        if (!mb) { SetStatus(L"Create failed."); return; }
        const bool isNew = (item.live == nullptr);
        const bool isMat = (mb->SuperClassID() == MATERIAL_CLASS_ID);
        const bool isMap = (mb->SuperClassID() == TEXMAP_CLASS_ID);

        // Name new instances
        if (isNew)
            mb->SetName(MSTR((item.label + L"_" +
                std::to_wstring((GetTickCount() % 9000) + 1000)).c_str()));

        // Get medit slot
        int slot = 0;
        if (IMtlEditInterface* me = GetMtlEditInterface())
            slot = std::max(0, me->GetActiveMtlSlot());

        theHold.Begin();

        if (drag)
        {
            // ── Drag path ───────────────────────────────────────
            // 1. Try DAD drop on any target (SME, medit palette slots, etc.)
            bool dropped = TryDADDrop(mb);

            if (!dropped && isMat)
            {
                // 2. Materials: assign to object under cursor via MaxScript
                ip->PutMtlToMtlEditor(mb, slot);
                ExecuteMAXScriptScript(kDragScript, MAXScript::ScriptSource::Dynamic);
                dropped = true;
            }

            if (!dropped)
            {
                // 3. Fallback: put in medit palette
                ip->PutMtlToMtlEditor(mb, slot);
            }

            Hide();
        }
        else
        {
            // ── Click path: context-aware placement ─────────────
            // Check if SME is open
            FPValue smeResult;
            BOOL smeOk = ExecuteMAXScriptScript(L"SME.isOpen()",
                MAXScript::ScriptSource::Dynamic, TRUE, &smeResult);
            bool smeOpen = smeOk && smeResult.type == TYPE_BOOL && smeResult.b;

            if (smeOpen) {
                // SME is open — drop into it (don't open it, it's already open)
                ip->PutMtlToMtlEditor(mb, slot);
                ExecuteMAXScriptScript(kSmeAtSpawnScript, MAXScript::ScriptSource::Dynamic);
            } else if (isMat && ip->GetSelNodeCount() > 0) {
                // Has selection + it's a material — assign to selected objects
                Mtl* mtl = static_cast<Mtl*>(mb);
                for (int i = 0; i < ip->GetSelNodeCount(); ++i)
                    if (INode* n = ip->GetSelNode(i)) n->SetMtl(mtl);
                ip->PutMtlToMtlEditor(mb, slot);
            } else if (GetMtlEditInterface()) {
                // Material palette exists — put in medit slot
                ip->PutMtlToMtlEditor(mb, slot);
            }
            // else: nothing selected, nothing open — do nothing
            Hide();
        }

        theHold.Accept(_T("Assign Material"));

        sceneCacheReady_ = false;
        ip->RedrawViews(ip->GetTime());
        SetStatus(drag ? L"Dropped." : L"Inserted.");
    }

    // ─── Helpers ────────────────────────────────────────────────
    void MoveSelection(int delta)
    {
        int count = static_cast<int>(SendMessageW(list_, LB_GETCOUNT, 0, 0));
        if (count <= 0) return;
        int cur = static_cast<int>(SendMessageW(list_, LB_GETCURSEL, 0, 0));
        if (cur == LB_ERR) cur = 0;
        else cur = std::clamp(cur + delta, 0, count - 1);
        SendMessageW(list_, LB_SETCURSEL, cur, 0);
    }

    void SetStatus(const std::wstring& s)
    {
        if (status_) SetWindowTextW(status_, s.c_str());
    }

    // ─── File-local pins (stored in max file via rootNode appData) ──
    std::wstring GetItemAlias(int filteredIdx)
    {
        if (!activeItems_ || filteredIdx < 0 ||
            filteredIdx >= static_cast<int>(filtered_.size())) return {};
        int si = filtered_[filteredIdx];
        if (si < 0 || si >= static_cast<int>(activeItems_->size())) return {};
        return (*activeItems_)[si].key;
    }
    std::wstring GetItemLabel(int filteredIdx)
    {
        if (!activeItems_ || filteredIdx < 0 ||
            filteredIdx >= static_cast<int>(filtered_.size())) return {};
        int si = filtered_[filteredIdx];
        if (si < 0 || si >= static_cast<int>(activeItems_->size())) return {};
        return (*activeItems_)[si].label;
    }

    void ToggleFilePin(int filteredIdx)
    {
        std::wstring alias = GetItemAlias(filteredIdx);
        if (alias.empty()) return;
        auto it = std::find(filePins_.begin(), filePins_.end(), alias);
        if (it != filePins_.end()) filePins_.erase(it);
        else filePins_.insert(filePins_.begin(), alias);
        SaveFilePins();
        Rebuild(true);
        SetStatus(it != filePins_.end() ? L"Unpinned." : L"Pinned to file.");
    }

    std::wstring GetPinCfgPath()
    {
        std::wstring s;
        FPValue result;
        BOOL ok = ExecuteMAXScriptScript(
            L"(getDir #plugcfg)+\"\\\\PowerShader_Pins.cfg\"",
            MAXScript::ScriptSource::Dynamic, TRUE, &result);
        if (ok && result.type == TYPE_STRING && result.s)
            s = result.s;
        return s;
    }

    void SaveFilePins()
    {
        std::wstring path = GetPinCfgPath();
        if (path.empty()) return;
        FILE* f = _wfopen(path.c_str(), L"w");
        if (!f) return;
        for (auto& pin : filePins_)
            fwprintf(f, L"%s\n", pin.c_str());
        fclose(f);
    }

    void LoadFilePins()
    {
        filePins_.clear();
        std::wstring path = GetPinCfgPath();
        if (path.empty()) return;
        FILE* f = _wfopen(path.c_str(), L"r");
        if (!f) return;
        wchar_t line[256];
        while (fgetws(line, 256, f)) {
            std::wstring l(line);
            while (!l.empty() && (l.back() == L'\n' || l.back() == L'\r')) l.pop_back();
            if (!l.empty()) filePins_.push_back(l);
        }
        fclose(f);
    }

    // ─── Persistent brick favorites (saved in PowerShader.cfg) ──
    void ToggleBrickFav(int filteredIdx)
    {
        std::wstring alias = GetItemAlias(filteredIdx);
        if (alias.empty()) return;
        // Check if already a brick fav
        auto it = std::find_if(brickFavs_.begin(), brickFavs_.end(),
            [&](const BrickFav& bf) { return bf.alias == alias; });
        if (it != brickFavs_.end()) {
            brickFavs_.erase(it);
            SetStatus(L"Removed from favorites.");
        } else {
            if (static_cast<int>(brickFavs_.size()) >= kBrickMax) {
                SetStatus(L"Max favorites reached.");
                return;
            }
            std::wstring lbl = GetItemLabel(filteredIdx);
            // Default label: first 3 letters uppercase
            std::wstring shortLbl;
            for (size_t i = 0; i < lbl.size() && shortLbl.size() < 3; i++)
                if (iswalnum(lbl[i])) shortLbl += towupper(lbl[i]);
            brickFavs_.push_back({alias, shortLbl});
            SetStatus(L"Added to favorites.");
        }
        SaveBrickFavs();
        RebuildBrickUI();
    }

    void RemoveBrickFav(int brickIdx)
    {
        if (brickIdx < 0 || brickIdx >= static_cast<int>(brickFavs_.size())) return;
        brickFavs_.erase(brickFavs_.begin() + brickIdx);
        SaveBrickFavs();
        RebuildBrickUI();
        SetStatus(L"Removed from favorites.");
    }

    std::wstring GetBrickCfgPath()
    {
        std::wstring s;
        FPValue result;
        BOOL ok = ExecuteMAXScriptScript(
            L"(getDir #plugcfg)+\"\\\\PowerShader.cfg\"",
            MAXScript::ScriptSource::Dynamic, TRUE, &result);
        if (ok && result.type == TYPE_STRING && result.s)
            s = result.s;
        return s;
    }

    void SaveBrickFavs()
    {
        std::wstring path = GetBrickCfgPath();
        if (path.empty()) return;
        FILE* f = _wfopen(path.c_str(), L"w");
        if (!f) return;
        for (auto& bf : brickFavs_)
            fwprintf(f, L"%s|%s\n", bf.alias.c_str(), bf.label.c_str());
        fclose(f);
    }

    void LoadBrickFavs()
    {
        brickFavs_.clear();
        std::wstring path = GetBrickCfgPath();
        if (path.empty()) return;
        FILE* f = _wfopen(path.c_str(), L"r");
        if (!f) return;
        wchar_t line[256];
        while (fgetws(line, 256, f)) {
            std::wstring l(line);
            while (!l.empty() && (l.back() == L'\n' || l.back() == L'\r')) l.pop_back();
            size_t sep = l.find(L'|');
            if (sep == std::wstring::npos) continue;
            brickFavs_.push_back({l.substr(0, sep), l.substr(sep + 1)});
        }
        fclose(f);
    }

    void RebuildBrickUI()
    {
        // Destroy existing brick buttons
        for (HWND bh : brickBtns_)
            if (bh) DestroyWindow(bh);
        brickBtns_.clear();
        if (!wnd_) return;

        RECT cr; GetClientRect(wnd_, &cr);
        int pad = 8, cw = cr.right - 2 * pad;

        if (brickFavs_.empty()) {
            // No bricks — restore list to original position
            SetWindowPos(list_, nullptr, pad, listBaseY_, cw,
                (cr.bottom - 26) - listBaseY_, SWP_NOZORDER);
            InvalidateRect(wnd_, nullptr, TRUE);
            return;
        }

        // Calculate brick layout — buttons stretch to fill row width
        int bwMin = 50, bh2 = 22, gap = 3;
        int cols = std::max(1, (cw + gap) / (bwMin + gap));
        int bw = (cw - (cols - 1) * gap) / cols; // stretch to fill
        int x = pad, y2 = listBaseY_;
        int maxY = y2;
        int col = 0;
        for (int i = 0; i < static_cast<int>(brickFavs_.size()) && i < kBrickMax; i++) {
            if (col >= cols) { col = 0; x = pad; y2 += bh2 + gap; }
            HWND btn = CreateWindowExW(0, L"BUTTON", brickFavs_[i].label.c_str(),
                WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                x, y2, bw, bh2, wnd_,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kBrickBase + i)),
                hInstance, nullptr);
            SetWindowSubclass(btn, BrickBtnProc, 1, reinterpret_cast<DWORD_PTR>(this));
            brickBtns_.push_back(btn);
            maxY = y2 + bh2 + gap;
            x += bw + gap;
            col++;
        }

        // Move list below brick area
        SetWindowPos(list_, nullptr, pad, maxY, cw,
            (cr.bottom - 26) - maxY, SWP_NOZORDER);
        InvalidateRect(wnd_, nullptr, TRUE);
    }

    void RenameBrickFav(int brickIdx)
    {
        if (brickIdx < 0 || brickIdx >= static_cast<int>(brickFavs_.size())) return;
        // Simple inline rename: prompt via small EDIT overlay
        HWND btn = brickBtns_[brickIdx];
        RECT br; GetWindowRect(btn, &br);
        POINT p = {br.left, br.top}; ScreenToClient(wnd_, &p);
        HWND ed = CreateWindowExW(0, L"EDIT", brickFavs_[brickIdx].label.c_str(),
            WS_CHILD | WS_VISIBLE | WS_BORDER | ES_CENTER | ES_AUTOHSCROLL,
            p.x, p.y, 50, 22, wnd_, nullptr, hInstance, nullptr);
        SendMessageW(ed, WM_SETFONT, reinterpret_cast<WPARAM>(Theme::fontUI), TRUE);
        SendMessageW(ed, EM_SETLIMITTEXT, 4, 0); // max 4 chars
        SendMessageW(ed, EM_SETSEL, 0, -1);
        SetFocus(ed);
        // Store info for completion
        renameEdit_ = ed;
        renameIdx_ = brickIdx;
    }

    void FinishRename()
    {
        if (!renameEdit_ || renameIdx_ < 0) return;
        wchar_t buf[8] = {};
        GetWindowTextW(renameEdit_, buf, 8);
        std::wstring lbl(buf);
        if (!lbl.empty() && renameIdx_ < static_cast<int>(brickFavs_.size())) {
            brickFavs_[renameIdx_].label = lbl;
            SaveBrickFavs();
            if (renameIdx_ < static_cast<int>(brickBtns_.size()))
                SetWindowTextW(brickBtns_[renameIdx_], lbl.c_str());
        }
        DestroyWindow(renameEdit_);
        renameEdit_ = nullptr;
        renameIdx_ = -1;
    }

    // ─── State ──────────────────────────────────────────────────
    HWND hotkeyWnd_ = nullptr;
    HWND wnd_       = nullptr;
    HWND edit_      = nullptr;
    HWND list_      = nullptr;
    HWND link_      = nullptr;
    HWND shll_      = nullptr;
    HWND scene_     = nullptr;
    HWND status_    = nullptr;
    TabMode tab_    = TabMode::All;
    std::vector<Item> classItems_;
    std::vector<Item> sceneItems_;
    const std::vector<Item>* activeItems_ = nullptr;
    std::vector<int> filtered_;
    std::wstring lastQuery_;
    TabMode lastTab_ = TabMode::All;
    bool lastSceneOnly_ = false;
    bool classCacheReady_ = false;
    bool forcedAliasRetry_ = false;
    bool sceneCacheReady_ = false;
    bool rebuildPending_ = false;
    bool  dragging_  = false;
    int   dragIndex_ = -1;
    POINT dragStart_ = {};
    RECT  closeRect_ = {};
    bool  hoverClose_ = false;
    bool  trackingMouse_ = false;
    bool  sceneOnly_  = false;
    int   listBaseY_  = 0;
    // Dual favorites
    std::vector<std::wstring> filePins_;     // file-local (per max file)
    std::vector<BrickFav> brickFavs_;        // persistent (config file)
    std::vector<HWND> brickBtns_;            // brick button HWNDs
    HWND  renameEdit_ = nullptr;
    int   renameIdx_  = -1;
    int   brickDragId_   = -1;
    bool  brickDragging_ = false;
    int   shllRes_       = 256; // SHLL preview resolution
};

} // anonymous namespace

// ── Exported API ────────────────────────────────────────────────
void Init(HINSTANCE) { Palette::Get().Init(true); }
void Shutdown()      { Palette::Get().Shutdown(); }
void Toggle()        { Palette::Get().Toggle(); }
bool IsOpen()        { return false; } // TODO: add accessor to Palette
void ReloadTheme(bool lightTheme) { Theme::Update(lightTheme); }

} // namespace PowerShader
