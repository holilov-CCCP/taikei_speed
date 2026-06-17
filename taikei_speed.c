/*
  Build (MinGW):
    x86_64-w64-mingw32-gcc taikei_speed.c -o taikei_speed.exe \
       -municode -mwindows -O2 -lwinhttp -lcomctl32 -lgdi32 -lcomdlg32 -lshell32 -limm32
  Build (MSVC, x64 Native Tools):
    cl /utf-8 /DUNICODE /D_UNICODE taikei_speed.c ^
       user32.lib gdi32.lib comdlg32.lib winhttp.lib comctl32.lib shell32.lib imm32.lib ^
       /link /SUBSYSTEM:WINDOWS
*/
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <winhttp.h>
#include <imm.h> 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define COLS         40
#define MAXLEN       40000
#define GRID_PADX    8
#define GRID_PADY    6
#define PREP_SEC     5
#define RUN_SEC      600

#define ID_REFPATH   1001
#define ID_BROWSE    1002
#define ID_LOAD      1003
#define ID_REFTEXT   1004
#define ID_START     1005
#define ID_STOP      1006
#define ID_RESET     1007
#define ID_GRID      1008
#define ID_RESULT    1009
#define ID_COPY      1010
#define ID_LANG      1011
#define ID_ZEN       1012
#define ID_TIMEROBJ  1

enum { ST_IDLE, ST_PREP, ST_RUNNING, ST_FINISHED, ST_STOPPED };

typedef struct {
    HWND hwnd;
    wchar_t buf[MAXLEN+1];
    int buflen;
    int caret;
    int selStart;
    int selEnd;
    int scrollRow;
    int scrollX;
    BOOL caretShown;
    BOOL isEnabled;
    BOOL isSelecting;
    int cellW;
    int cellH;
    HFONT hFont;
} GRIDSTATE;

static HINSTANCE g_hInst;
static HWND  g_hMain, g_hRefPath, g_hBrowse, g_hLoad, g_hZen;
static HWND  g_hOverlay;            
static HBRUSH g_brOverlay = NULL;   
static HWND  g_hStatus, g_hTimerLbl, g_hStart, g_hStop, g_hReset;
static HWND  g_hResult, g_hCopy;
static HWND  g_lblRef, g_lblGrid, g_lblResult, g_lblDisclaimer;
static HFONT g_fontUI, g_fontGridIn, g_fontGridRef, g_fontTimer, g_fontMono, g_fontHuge;

static GRIDSTATE g_refGrid;
static GRIDSTATE g_inGrid;

static wchar_t g_refStrip[MAXLEN+1];
static int     g_refStripLen = 0;
static BOOL    g_refLoaded = FALSE;

static int        g_state = ST_IDLE;
static ULONGLONG  g_phaseEnd = 0;
static ULONGLONG  g_runStart = 0;
static ULONGLONG  g_stopTick = 0;
static int        g_lastShownSec = -1;
static BOOL       g_scoreDirty = FALSE;
static BOOL       g_japanese = TRUE;

static int g_dpA[MAXLEN+1];
static int g_dpB[MAXLEN+1];
static int *g_dpPrev = g_dpA, *g_dpCur = g_dpB;

static wchar_t g_resultText[4096];

static const char *PS_SCRIPT =
"param([string]$Pdf,[string]$Lang,[string]$Out)\r\n"
"$ErrorActionPreference='Stop'\r\n"
"try{\r\n"
"function Await($t,$rt){\r\n"
"  $m=[System.WindowsRuntimeSystemExtensions].GetMethods()|?{ $_.Name -eq 'AsTask' -and $_.GetParameters().Count -eq 1 -and $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation`1' }\r\n"
"  $g=$m[0].MakeGenericMethod($rt); $task=$g.Invoke($null,@($t)); $task.Wait(-1)|Out-Null; return $task.Result }\r\n"
"function AwaitAction($a){\r\n"
"  $m=[System.WindowsRuntimeSystemExtensions].GetMethods()|?{ $_.Name -eq 'AsTask' -and $_.GetParameters().Count -eq 1 -and $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncAction' }\r\n"
"  $task=$m[0].Invoke($null,@($a)); $task.Wait(-1)|Out-Null }\r\n"
"[void][Windows.Data.Pdf.PdfDocument,Windows.Data.Pdf,ContentType=WindowsRuntime]\r\n"
"[void][Windows.Data.Pdf.PdfPageRenderOptions,Windows.Data.Pdf,ContentType=WindowsRuntime]\r\n"
"[void][Windows.Media.Ocr.OcrEngine,Windows.Media.Ocr,ContentType=WindowsRuntime]\r\n"
"[void][Windows.Graphics.Imaging.BitmapDecoder,Windows.Graphics.Imaging,ContentType=WindowsRuntime]\r\n"
"[void][Windows.Storage.StorageFile,Windows.Storage,ContentType=WindowsRuntime]\r\n"
"[void][Windows.Storage.Streams.InMemoryRandomAccessStream,Windows.Storage.Streams,ContentType=WindowsRuntime]\r\n"
"[void][Windows.Globalization.Language,Windows.Globalization,ContentType=WindowsRuntime]\r\n"
"$file=Await ([Windows.Storage.StorageFile]::GetFileFromPathAsync($Pdf)) ([Windows.Storage.StorageFile])\r\n"
"$doc =Await ([Windows.Data.Pdf.PdfDocument]::LoadFromFileAsync($file)) ([Windows.Data.Pdf.PdfDocument])\r\n"
"$engine=$null\r\n"
"try{ $engine=[Windows.Media.Ocr.OcrEngine]::TryCreateFromLanguage((New-Object Windows.Globalization.Language($Lang))) }catch{}\r\n"
"if(-not $engine){ try{ $engine=[Windows.Media.Ocr.OcrEngine]::TryCreateFromUserProfileLanguages() }catch{} }\r\n"
"if(-not $engine){ Set-Content -LiteralPath $Out -Value '__OCR_LANG_UNAVAILABLE__' -Encoding UTF8; exit 2 }\r\n"
"$sb=New-Object System.Text.StringBuilder\r\n"
"for($i=0;$i -lt $doc.PageCount;$i++){\r\n"
"  $page=$doc.GetPage($i)\r\n"
"  $stream=New-Object Windows.Storage.Streams.InMemoryRandomAccessStream\r\n"
"  $opt=New-Object Windows.Data.Pdf.PdfPageRenderOptions\r\n"
"  $scale=4.86\r\n"
"  $opt.DestinationWidth=[uint32]([math]::Round([double]$page.Size.Width * $scale))\r\n"
"  AwaitAction ($page.RenderToStreamAsync($stream,$opt))\r\n"
"  $dec=Await ([Windows.Graphics.Imaging.BitmapDecoder]::CreateAsync($stream)) ([Windows.Graphics.Imaging.BitmapDecoder])\r\n"
"  $bmp=Await ($dec.GetSoftwareBitmapAsync()) ([Windows.Graphics.Imaging.SoftwareBitmap])\r\n"
"  $res=Await ($engine.RecognizeAsync($bmp)) ([Windows.Media.Ocr.OcrResult])\r\n"
"  $rows=New-Object System.Collections.Generic.List[object]\r\n"
"  $hsum=0.0; $hcnt=0\r\n"
"  foreach($ln in $res.Lines){\r\n"
"    if($ln.Words.Count -eq 0){ continue }\r\n"
"    $minX=[double]::MaxValue; $top=[double]::MaxValue; $bot=[double]::MinValue\r\n"
"    foreach($w in $ln.Words){ $r=$w.BoundingRect; if($r.X -lt $minX){$minX=$r.X}; if($r.Y -lt $top){$top=$r.Y}; if(($r.Y+$r.Height) -gt $bot){$bot=$r.Y+$r.Height}; $hsum+=$r.Height; $hcnt++ }\r\n"
"    $t=$ln.Text.Trim()\r\n"
"    if($t.Length -eq 0){ continue }\r\n"
"    if($t -match '^[\\d\\uFF10-\\uFF19 .,\\uFF0C\\u3000\\u3001\\u3002]+$'){ continue }\r\n"
"    [void]$rows.Add([pscustomobject]@{ T=$ln.Text; X=$minX; CY=(($top+$bot)/2.0) })\r\n"
"  }\r\n"
"  $tol=20.0; if($hcnt -gt 0){ $tol=($hsum/$hcnt)*0.6 }\r\n"
"  $bk=New-Object System.Collections.Generic.List[object]; $bky=$null\r\n"
"  foreach($it in ($rows | Sort-Object CY)){\r\n"
"    if($bky -ne $null -and ($it.CY-$bky) -gt $tol){ [void]$sb.AppendLine((($bk | Sort-Object X | ForEach-Object{$_.T}) -join ' ')); $bk=New-Object System.Collections.Generic.List[object]; $bky=$null }\r\n"
"    if($bky -eq $null){ $bky=$it.CY }\r\n"
"    [void]$bk.Add($it)\r\n"
"  }\r\n"
"  if($bk.Count -gt 0){ [void]$sb.AppendLine((($bk | Sort-Object X | ForEach-Object{$_.T}) -join ' ')) }\r\n"
"  $bmp.Dispose(); $page.Dispose(); $stream.Dispose()\r\n"
"}\r\n"
"Set-Content -LiteralPath $Out -Value $sb.ToString() -Encoding UTF8\r\n"
"exit 0\r\n"
"}catch{ Set-Content -LiteralPath $Out -Value ('__OCR_ERROR__ '+$_.Exception.Message) -Encoding UTF8; exit 3 }\r\n";

static int is_zen_space(wchar_t c){ return c==0x3000; }
static int is_ws(wchar_t c){ return c==L' '||c==L'\t'||is_zen_space(c)||c==L'\r'; }
static int is_digit_any(wchar_t c){ return (c>=L'0'&&c<=L'9')||(c>=0xFF10&&c<=0xFF19); }
static int is_comma_any(wchar_t c){ return c==L','||c==0xFF0C; }
static wchar_t to_zen_digit(wchar_t c){ return (c>=L'0'&&c<=L'9')?(wchar_t)(c-L'0'+0xFF10):c; }

static int count_mistakes(const wchar_t *typed, int tlen, const wchar_t *ref, int rlen)
{
    int i, j;
    if (rlen < 0) rlen = 0;
    if (tlen < 0) tlen = 0;
    g_dpPrev = g_dpA; g_dpCur = g_dpB;
    for (j = 0; j <= rlen; j++) g_dpPrev[j] = j;
    for (i = 1; i <= tlen; i++) {
        g_dpCur[0] = i;
        wchar_t tc = typed[i-1];
        for (j = 1; j <= rlen; j++) {
            int sub = g_dpPrev[j-1] + (tc == ref[j-1] ? 0 : 1);
            int ins = g_dpPrev[j] + 1;
            int del = g_dpCur[j-1] + 1;
            int m = sub; if (ins < m) m = ins; if (del < m) m = del;
            g_dpCur[j] = m;
        }
        int *t = g_dpPrev; g_dpPrev = g_dpCur; g_dpCur = t;
    }
    int best = g_dpPrev[0];
    for (j = 1; j <= rlen; j++) if (g_dpPrev[j] < best) best = g_dpPrev[j];
    return best;
}
static int strip_nl(const wchar_t *src, int n, wchar_t *dst){ int c=0,i; for(i=0;i<n;i++) if(src[i]!=L'\n'&&src[i]!=L'\r') dst[c++]=src[i]; return c; }

static const wchar_t* grade_label(int net, BOOL jp)
{
    if (jp) {
        if (net>=2000) return L"特段";
        if (net>=1500) return L"初段";
        if (net>=1000) return L"1級";
        if (net>=800)  return L"準1級";
        if (net>=600)  return L"2級";
        if (net>=450)  return L"準2級";
        if (net>=350)  return L"3級";
        if (net>=250)  return L"4級";
        if (net>=100)  return L"5級";
        if (net>=50)   return L"6級";
        return L"級外(認定なし)";
    } else {
        if (net>=4000) return L"特段";
        if (net>=3000) return L"初段";
        if (net>=2000) return L"1級";
        if (net>=1600) return L"準1級";
        if (net>=1200) return L"2級";
        if (net>=900)  return L"準2級";
        if (net>=700)  return L"3級";
        if (net>=500)  return L"4級";
        if (net>=200)  return L"5級";
        if (net>=100)  return L"6級";
        return L"級外(認定なし)";
    }
}

static void compute_score(int *outTotal, int *outMist, int *outNet)
{
    static wchar_t ts[MAXLEN+1];
    int tl = strip_nl(g_inGrid.buf, g_inGrid.buflen, ts);
    int total = tl, mist = 0;
    if (g_refLoaded && g_refStripLen > 0)
        mist = count_mistakes(ts, tl, g_refStrip, g_refStripLen);
    int deduct = g_japanese ? mist : mist*2;
    if (deduct > total) deduct = total;
    int net = total - deduct; if (net < 0) net = 0;
    *outTotal = total; *outMist = mist; *outNet = net;
}

static void refresh_reference_from_box(void)
{
    g_refStripLen = strip_nl(g_refGrid.buf, g_refGrid.buflen, g_refStrip);
    g_refStrip[g_refStripLen] = 0;
    g_refLoaded = (g_refStripLen > 0);
}

static void replace_string(wchar_t *str, const wchar_t *oldStr, const wchar_t *newStr, size_t max_len) {
    wchar_t *pos = str;
    int oldLen = (int)wcslen(oldStr);
    int newLen = (int)wcslen(newStr);
    wchar_t *temp = (wchar_t *)malloc(max_len * sizeof(wchar_t));
    if (!temp) return;
    
    while ((pos = wcsstr(pos, oldStr)) != NULL) {
        if (wcslen(str) - oldLen + newLen >= max_len - 1) break; 
        wcscpy(temp, pos + oldLen);
        wcscpy(pos, newStr);
        wcscpy(pos + newLen, temp);
        pos += newLen;
    }
    free(temp);
}

static wchar_t* clean_ocr_text(const wchar_t *in, BOOL jp)
{
    size_t cap = wcslen(in) + 8192; 
    wchar_t *out = (wchar_t*)malloc(cap * sizeof(wchar_t));
    if (!out) return NULL;
    size_t o = 0;
    const wchar_t *p = in;
    static wchar_t line[8192];
    static wchar_t temp_line[8192];

    BOOL prev_ends_with_maru = TRUE; 

    while (*p) {
        int ll = 0;
        while (*p && *p!=L'\n' && ll < 8190) { if(*p!=L'\r') line[ll++]=*p; p++; }
        while (*p && *p!=L'\n') p++;
        if (*p==L'\n') p++;
        line[ll]=0;
        
        while (ll>0 && is_ws(line[ll-1])) ll--;
        line[ll]=0;
        if (ll==0) continue;

        int tl = 0, k;
        if (jp) {
            for (k=0;k<ll;k++){
                wchar_t c=line[k];
                if(is_ws(c)) continue;
                temp_line[tl++]=to_zen_digit(c);
            }
            temp_line[tl]=0;
        } else {
            for (k=0;k<ll;k++) temp_line[tl++]=line[k];
            temp_line[tl]=0;
        }

        if (tl==0) continue;

        int digit_count = 0;
        int only_trash = 1;
        for (k=0; k<tl; k++){
            wchar_t c = temp_line[k];
            if (is_digit_any(c) || is_comma_any(c)) {
                digit_count++;
            } else if (!(is_ws(c) || c == L'|' || c == L'/' || c == L'l' || c == L'I' || 
                         c == L'ー' || c == L'-' || c == 0xFF5C || c == 0xFF0F || c == L'、' || c == L'。')) {
                only_trash = 0; 
            }
        }
        if (only_trash) continue;
        
        if (tl <= 10 && digit_count >= tl - 2) continue;

        BOOL is_header = FALSE;
        if (wcsstr(temp_line, L"日本情報処理検定協会") && (wcsstr(temp_line, L"主催") || wcsstr(temp_line, L"後援"))) is_header = TRUE;
        if (wcsstr(temp_line, L"認定試験") && wcsstr(temp_line, L"第") && wcsstr(temp_line, L"回")) is_header = TRUE;
        if (wcsstr(temp_line, L"文部科学省") && tl < 30) is_header = TRUE;
        if (wcsstr(temp_line, L"問題その") && tl <= 10) is_header = TRUE;
        if (wcsstr(temp_line, L"受験番号") && wcsstr(temp_line, L"氏名")) is_header = TRUE;
        if (wcscmp(temp_line, L"問題") == 0) is_header = TRUE;
        if (wcsncmp(temp_line, L"その", 2) == 0 && tl <= 5) is_header = TRUE;

        if (is_header) continue;

        if (jp) {
            if (o + tl + 3 > cap) { cap=(o+tl+3)*2; wchar_t*nb=(wchar_t*)realloc(out,cap*sizeof(wchar_t)); if(!nb){free(out);return NULL;} out=nb; }
            
            if (prev_ends_with_maru) {
                if (o > 0) {
                    out[o++] = L'\n';
                }
                out[o++] = 0x3000; 
            }
            
            memcpy(out+o, temp_line, tl*sizeof(wchar_t)); o+=tl;
            
            wchar_t last_char = temp_line[tl-1];
            if (last_char == L'。' || last_char == L'！' || last_char == L'？' || last_char == 0xFF0E) {
                prev_ends_with_maru = TRUE;
            } else {
                prev_ends_with_maru = FALSE;
            }
        } else {
            if (o + tl + 2 > cap) { cap=(o+tl+2)*2; wchar_t*nb=(wchar_t*)realloc(out,cap*sizeof(wchar_t)); if(!nb){free(out);return NULL;} out=nb; }
            memcpy(out+o, temp_line, tl*sizeof(wchar_t)); o+=tl;
            out[o++]=L'\n';
        }
    }
    
    if (jp && o > 0) {
        out[o++] = L'\n';
    }
    out[o]=0;

    if (jp) {
        replace_string(out, L"-", L"ー", cap);
    }

    return out;
}

static void zenkakuify_box(void)
{
    for(int i=0; i<g_refGrid.buflen; i++) {
        g_refGrid.buf[i] = to_zen_digit(g_refGrid.buf[i]);
    }
    refresh_reference_from_box();
    InvalidateRect(g_refGrid.hwnd, NULL, FALSE);
}

static void index_to_rowcol(GRIDSTATE* ctx, int idx, int*row, int*col)
{
    int r=0,c=0,i;
    for(i=0;i<idx && i<ctx->buflen;i++){ wchar_t ch=ctx->buf[i];
        if(ch==L'\n'){ r++; c=0; } else { c++; if(c==COLS){ r++; c=0; } } }
    *row=r; *col=c;
}
static int total_rows(GRIDSTATE* ctx)
{
    int r=0,c=0,i;
    for(i=0;i<ctx->buflen;i++){ wchar_t ch=ctx->buf[i];
        if(ch==L'\n'){ r++; c=0; } else { c++; if(c==COLS){ r++; c=0; } } }
    return r+1;
}
static int rowcol_to_index(GRIDSTATE* ctx, int targetRow, int targetCol)
{
    int r=0,c=0,i,cand=0,have=0;
    for(i=0;i<=ctx->buflen;i++){
        if(r==targetRow){ if(c==targetCol) return i; if(c<targetCol){ cand=i; have=1; } }
        if(r>targetRow) break;
        if(i==ctx->buflen) break;
        wchar_t ch=ctx->buf[i];
        if(ch==L'\n'){ r++; c=0; } else { c++; if(c==COLS){ r++; c=0; } }
    }
    return have?cand:ctx->buflen;
}
static int end_of_row_index(GRIDSTATE* ctx, int row)
{
    int idx=rowcol_to_index(ctx,row,0);
    while(idx<ctx->buflen){ int rr,cc; index_to_rowcol(ctx,idx,&rr,&cc); if(rr!=row)break; if(ctx->buf[idx]==L'\n')break; idx++; }
    return idx;
}

static void grid_update_scrollbar(GRIDSTATE* ctx)
{
    int tr=total_rows(ctx);
    RECT rc; GetClientRect(ctx->hwnd,&rc);
    int visRows=(rc.bottom-GRID_PADY)/ctx->cellH; if(visRows<1)visRows=1;
    SCROLLINFO si; ZeroMemory(&si,sizeof si); si.cbSize=sizeof si;
    si.fMask=SIF_RANGE|SIF_PAGE|SIF_POS;
    si.nMin=0; si.nMax=(tr>0?tr-1:0); si.nPage=visRows; si.nPos=ctx->scrollRow;
    SetScrollInfo(ctx->hwnd,SB_VERT,&si,TRUE);
    int contentW=COLS*ctx->cellW+GRID_PADX*2; int cw=rc.right; if(cw<1)cw=1;
    SCROLLINFO sh; ZeroMemory(&sh,sizeof sh); sh.cbSize=sizeof sh;
    sh.fMask=SIF_RANGE|SIF_PAGE|SIF_POS;
    sh.nMin=0; sh.nMax=(contentW>0?contentW-1:0); sh.nPage=cw; sh.nPos=ctx->scrollX;
    SetScrollInfo(ctx->hwnd,SB_HORZ,&sh,TRUE);
}
static void grid_set_caret_pos(GRIDSTATE* ctx)
{
    if(!ctx->caretShown) return;
    int row,col; index_to_rowcol(ctx,ctx->caret,&row,&col);
    RECT rc; GetClientRect(ctx->hwnd,&rc);
    int visRows=(rc.bottom-GRID_PADY)/ctx->cellH; if(visRows<1)visRows=1;
    int x=GRID_PADX - ctx->scrollX + col*ctx->cellW + 1;
    int y=GRID_PADY+(row-ctx->scrollRow)*ctx->cellH+3;
    if(row<ctx->scrollRow || row>=ctx->scrollRow+visRows || x<-2 || x>rc.right){ SetCaretPos(-100,-100); return; }
    SetCaretPos(x,y);

    HIMC hImc = ImmGetContext(ctx->hwnd);
    if(hImc){
        COMPOSITIONFORM cf;
        cf.dwStyle = CFS_POINT;
        cf.ptCurrentPos.x = x;
        cf.ptCurrentPos.y = y;
        ImmSetCompositionWindow(hImc, &cf);

        LOGFONTW lf;
        if(GetObjectW(ctx->hFont, sizeof(LOGFONTW), &lf)){
            ImmSetCompositionFontW(hImc, &lf);
        }
        ImmReleaseContext(ctx->hwnd, hImc);
    }
}
static void grid_ensure_visible(GRIDSTATE* ctx)
{
    int row,col; index_to_rowcol(ctx,ctx->caret,&row,&col);
    RECT rc; GetClientRect(ctx->hwnd,&rc);
    int visRows=(rc.bottom-GRID_PADY)/ctx->cellH; if(visRows<1)visRows=1;
    if(row<ctx->scrollRow) ctx->scrollRow=row;
    else if(row>=ctx->scrollRow+visRows) ctx->scrollRow=row-visRows+1;
    if(ctx->scrollRow<0) ctx->scrollRow=0;
    int caretX=GRID_PADX+col*ctx->cellW; int cw=rc.right;
    int contentW=COLS*ctx->cellW+GRID_PADX*2; int maxX=contentW-cw; if(maxX<0)maxX=0;
    if(caretX<ctx->scrollX) ctx->scrollX=caretX;
    else if(caretX+ctx->cellW>ctx->scrollX+cw) ctx->scrollX=caretX+ctx->cellW-cw;
    if(ctx->scrollX<0)ctx->scrollX=0;
    if(ctx->scrollX>maxX)ctx->scrollX=maxX;
    grid_update_scrollbar(ctx);
}
static void grid_after_change(GRIDSTATE* ctx)
{
    if (ctx == &g_inGrid) g_scoreDirty = TRUE;
    if (ctx == &g_refGrid) refresh_reference_from_box();
    grid_ensure_visible(ctx);
    InvalidateRect(ctx->hwnd,NULL,FALSE);
    grid_set_caret_pos(ctx);
}

static void delete_selection(GRIDSTATE* ctx)
{
    if (ctx->selStart == ctx->selEnd) return;
    int minS = ctx->selStart < ctx->selEnd ? ctx->selStart : ctx->selEnd;
    int maxS = ctx->selStart > ctx->selEnd ? ctx->selStart : ctx->selEnd;
    int len = maxS - minS;
    memmove(&ctx->buf[minS], &ctx->buf[maxS], (ctx->buflen - maxS) * sizeof(wchar_t));
    ctx->buflen -= len;
    ctx->caret = ctx->selStart = ctx->selEnd = minS;
}

static void grid_insert_char(GRIDSTATE* ctx, wchar_t ch)
{
    if(ctx->buflen>=MAXLEN){ MessageBeep(0); return; }
    memmove(&ctx->buf[ctx->caret+1],&ctx->buf[ctx->caret],(ctx->buflen-ctx->caret)*sizeof(wchar_t));
    ctx->buf[ctx->caret]=ch; ctx->buflen++; ctx->caret++;
    ctx->selStart = ctx->selEnd = ctx->caret;
}
static void grid_insert_str(GRIDSTATE* ctx, const wchar_t* s)
{
    for(; *s; s++){ if(*s==L'\r') continue; if(ctx->buflen>=MAXLEN){ MessageBeep(0); break; }
        memmove(&ctx->buf[ctx->caret+1],&ctx->buf[ctx->caret],(ctx->buflen-ctx->caret)*sizeof(wchar_t));
        ctx->buf[ctx->caret]=*s; ctx->buflen++; ctx->caret++; }
    ctx->selStart = ctx->selEnd = ctx->caret;
}
static void grid_backspace(GRIDSTATE* ctx){ if(ctx->caret>0){ memmove(&ctx->buf[ctx->caret-1],&ctx->buf[ctx->caret],(ctx->buflen-ctx->caret)*sizeof(wchar_t)); ctx->buflen--; ctx->caret--; ctx->selStart=ctx->selEnd=ctx->caret; } }
static void grid_delete(GRIDSTATE* ctx){ if(ctx->caret<ctx->buflen){ memmove(&ctx->buf[ctx->caret],&ctx->buf[ctx->caret+1],(ctx->buflen-ctx->caret-1)*sizeof(wchar_t)); ctx->buflen--; ctx->selStart=ctx->selEnd=ctx->caret; } }

static void grid_paste(GRIDSTATE* ctx)
{
    if(!OpenClipboard(ctx->hwnd)) return;
    HANDLE h=GetClipboardData(CF_UNICODETEXT);
    if(h){ 
        wchar_t* pp=(wchar_t*)GlobalLock(h); 
        if(pp){ 
            if (ctx->selStart != ctx->selEnd) delete_selection(ctx);
            grid_insert_str(ctx,pp); 
            grid_after_change(ctx); 
        } 
        GlobalUnlock(h); 
    }
    CloseClipboard();
}
static void grid_copy_all(GRIDSTATE* ctx)
{
    int minS = ctx->selStart < ctx->selEnd ? ctx->selStart : ctx->selEnd;
    int maxS = ctx->selStart > ctx->selEnd ? ctx->selStart : ctx->selEnd;
    if (minS == maxS) {
        minS = 0; maxS = ctx->buflen;
    }
    int len = maxS - minS;
    if (len == 0) return;

    size_t bytes=(len+1)*sizeof(wchar_t);
    HGLOBAL hg=GlobalAlloc(GMEM_MOVEABLE,bytes); if(!hg)return;
    wchar_t* d=(wchar_t*)GlobalLock(hg); 
    memcpy(d, &ctx->buf[minS], len*sizeof(wchar_t)); 
    d[len]=0; 
    GlobalUnlock(hg);
    
    if(OpenClipboard(ctx->hwnd)){ EmptyClipboard(); SetClipboardData(CF_UNICODETEXT,hg); CloseClipboard(); }
    else GlobalFree(hg);
}

static void grid_paint(GRIDSTATE* ctx)
{
    PAINTSTRUCT ps; HDC hdc=BeginPaint(ctx->hwnd,&ps);
    RECT rc; GetClientRect(ctx->hwnd,&rc);
    HDC mem=CreateCompatibleDC(hdc);
    HBITMAP bmp=CreateCompatibleBitmap(hdc,rc.right,rc.bottom);
    HBITMAP oldb=(HBITMAP)SelectObject(mem,bmp);

    BOOL allowEdit = (ctx == &g_refGrid) ? TRUE : (g_state == ST_RUNNING);
    HBRUSH bg=CreateSolidBrush(allowEdit?RGB(255,255,255):RGB(244,244,244));
    FillRect(mem,&rc,bg); DeleteObject(bg);

    int visRows=(rc.bottom-GRID_PADY)/ctx->cellH + 1;
    HPEN pen=CreatePen(PS_SOLID,1,RGB(222,222,222));
    HPEN oldp=(HPEN)SelectObject(mem,pen);
    int x0=GRID_PADX - ctx->scrollX;
    for(int vr=0; vr<visRows; vr++){
        int r=ctx->scrollRow+vr;
        int y=GRID_PADY+vr*ctx->cellH;
        if(y>rc.bottom) break;
        MoveToEx(mem,x0,y,NULL); LineTo(mem,x0+COLS*ctx->cellW,y);
        MoveToEx(mem,x0,y+ctx->cellH,NULL); LineTo(mem,x0+COLS*ctx->cellW,y+ctx->cellH);
        for(int c=0;c<=COLS;c++){ int x=x0+c*ctx->cellW; MoveToEx(mem,x,y,NULL); LineTo(mem,x,y+ctx->cellH); }
        (void)r;
    }
    SelectObject(mem,oldp); DeleteObject(pen);

    HPEN pen2=CreatePen(PS_SOLID,1,RGB(180,180,180));
    oldp=(HPEN)SelectObject(mem,pen2);
    for(int vr=0; vr<visRows; vr++){
        int y=GRID_PADY+vr*ctx->cellH; if(y>rc.bottom)break;
        for(int c=0;c<=COLS;c+=10){ int x=x0+c*ctx->cellW; MoveToEx(mem,x,y,NULL); LineTo(mem,x,y+ctx->cellH); }
    }
    SelectObject(mem,oldp); DeleteObject(pen2);

    HFONT oldf=(HFONT)SelectObject(mem,ctx->hFont);
    
    int r=0,c=0;
    int minSel = ctx->selStart < ctx->selEnd ? ctx->selStart : ctx->selEnd;
    int maxSel = ctx->selStart > ctx->selEnd ? ctx->selStart : ctx->selEnd;

    for(int i=0;i<ctx->buflen;i++){
        wchar_t ch=ctx->buf[i];
        if(ch==L'\n'){ r++; c=0; continue; }
        if(r>=ctx->scrollRow && r<ctx->scrollRow+visRows){
            RECT cell; cell.left=x0+c*ctx->cellW; cell.top=GRID_PADY+(r-ctx->scrollRow)*ctx->cellH;
            cell.right=cell.left+ctx->cellW; cell.bottom=cell.top+ctx->cellH;
            
            BOOL inSel = (i >= minSel && i < maxSel);
            if (inSel) {
                SetBkMode(mem, OPAQUE);
                SetBkColor(mem, RGB(0, 120, 215));
                SetTextColor(mem, RGB(255, 255, 255));
            } else {
                SetBkMode(mem, TRANSPARENT);
                SetTextColor(mem, RGB(20, 20, 20));
            }

            DrawTextW(mem,&ch,1,&cell,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
        }
        c++; if(c==COLS){ r++; c=0; }
    }
    SelectObject(mem,oldf);

    BitBlt(hdc,0,0,rc.right,rc.bottom,mem,0,0,SRCCOPY);
    SelectObject(mem,oldb); DeleteObject(bmp); DeleteDC(mem);
    EndPaint(ctx->hwnd,&ps);
}

static LRESULT CALLBACK GridProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if(msg == WM_GETDLGCODE) return DLGC_WANTALLKEYS|DLGC_WANTCHARS|DLGC_WANTARROWS;

    GRIDSTATE* ctx = (GRIDSTATE*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if(!ctx) return DefWindowProcW(hwnd,msg,wp,lp);

    BOOL allowEdit = (ctx == &g_refGrid) ? TRUE : (g_state == ST_RUNNING);
    BOOL allowSelAndCopy = (ctx == &g_refGrid) ? TRUE : (g_state != ST_RUNNING && g_state != ST_PREP);

    switch(msg){
    case WM_SETFOCUS:
        CreateCaret(hwnd,NULL,2,ctx->cellH-6); ctx->caretShown=TRUE; grid_set_caret_pos(ctx); ShowCaret(hwnd);
        return 0;
    case WM_KILLFOCUS:
        HideCaret(hwnd); DestroyCaret(); ctx->caretShown=FALSE; return 0;
    case WM_SIZE: grid_update_scrollbar(ctx); grid_set_caret_pos(ctx); return 0;
    case WM_LBUTTONDOWN: {
        SetFocus(hwnd);
        int x=GET_X_LPARAM(lp), y=GET_Y_LPARAM(lp);
        int col=(x-GRID_PADX+ctx->scrollX)/ctx->cellW; if(col<0)col=0; if(col>COLS)col=COLS;
        int row=ctx->scrollRow+(y-GRID_PADY)/ctx->cellH; if(row<0)row=0;
        ctx->caret=rowcol_to_index(ctx,row,col);
        
        if (allowSelAndCopy) {
            SetCapture(hwnd);
            ctx->isSelecting = TRUE;
            if (GetKeyState(VK_SHIFT) & 0x8000) {
                ctx->selEnd = ctx->caret;
            } else {
                ctx->selStart = ctx->selEnd = ctx->caret;
            }
        } else {
            ctx->selStart = ctx->selEnd = ctx->caret;
            ctx->isSelecting = FALSE;
        }
        
        grid_set_caret_pos(ctx); InvalidateRect(hwnd,NULL,FALSE);
        return 0; }
    case WM_MOUSEMOVE: {
        if (ctx->isSelecting) {
            int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
            int col = (x - GRID_PADX + ctx->scrollX) / ctx->cellW; if(col < 0) col = 0; if(col > COLS) col = COLS;
            int row = ctx->scrollRow + (y - GRID_PADY) / ctx->cellH; if(row < 0) row = 0;
            ctx->caret = rowcol_to_index(ctx, row, col);
            ctx->selEnd = ctx->caret;
            grid_set_caret_pos(ctx);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0; }
    case WM_LBUTTONUP: {
        if (ctx->isSelecting) {
            ReleaseCapture();
            ctx->isSelecting = FALSE;
        }
        return 0; }
    case WM_MOUSEWHEEL: {
        int d=GET_WHEEL_DELTA_WPARAM(wp)/120;
        ctx->scrollRow -= d*3;
        int tr=total_rows(ctx); RECT rc; GetClientRect(hwnd,&rc);
        int visRows=(rc.bottom-GRID_PADY)/ctx->cellH; if(visRows<1)visRows=1;
        int maxs=tr-visRows; if(maxs<0)maxs=0;
        if(ctx->scrollRow<0)ctx->scrollRow=0;
        if(ctx->scrollRow>maxs)ctx->scrollRow=maxs;
        grid_update_scrollbar(ctx); InvalidateRect(hwnd,NULL,FALSE); grid_set_caret_pos(ctx);
        return 0; }
    case WM_VSCROLL: {
        RECT rc; GetClientRect(hwnd,&rc);
        int visRows=(rc.bottom-GRID_PADY)/ctx->cellH; if(visRows<1)visRows=1;
        int tr=total_rows(ctx); int maxs=tr-visRows; if(maxs<0)maxs=0;
        int pos=ctx->scrollRow;
        switch(LOWORD(wp)){
            case SB_LINEUP: pos--; break;
            case SB_LINEDOWN: pos++; break;
            case SB_PAGEUP: pos-=visRows; break;
            case SB_PAGEDOWN: pos+=visRows; break;
            case SB_THUMBTRACK: case SB_THUMBPOSITION: pos=HIWORD(wp); break;
            case SB_TOP: pos=0; break;
            case SB_BOTTOM: pos=maxs; break;
        }
        if(pos<0)pos=0;
        if(pos>maxs)pos=maxs;
        ctx->scrollRow=pos; grid_update_scrollbar(ctx); InvalidateRect(hwnd,NULL,FALSE); grid_set_caret_pos(ctx);
        return 0; }
    case WM_HSCROLL: {
        RECT rc; GetClientRect(hwnd,&rc); int cw=rc.right;
        int contentW=COLS*ctx->cellW+GRID_PADX*2; int maxX=contentW-cw; if(maxX<0)maxX=0;
        int pos=ctx->scrollX;
        switch(LOWORD(wp)){
            case SB_LINELEFT: pos-=ctx->cellW; break;
            case SB_LINERIGHT: pos+=ctx->cellW; break;
            case SB_PAGELEFT: pos-=cw; break;
            case SB_PAGERIGHT: pos+=cw; break;
            case SB_THUMBTRACK: case SB_THUMBPOSITION: pos=HIWORD(wp); break;
            case SB_LEFT: pos=0; break;
            case SB_RIGHT: pos=maxX; break;
        }
        if(pos<0)pos=0;
        if(pos>maxX)pos=maxX;
        ctx->scrollX=pos; grid_update_scrollbar(ctx); InvalidateRect(hwnd,NULL,FALSE); grid_set_caret_pos(ctx);
        return 0; }
    case WM_CHAR: {
        if(!allowEdit){ MessageBeep(0); return 0; }
        wchar_t ch=(wchar_t)wp;
        if(ch==8){ 
            if (ctx->selStart != ctx->selEnd) delete_selection(ctx);
            else grid_backspace(ctx); 
            grid_after_change(ctx); 
        }
        else if(ch==L'\r'||ch==L'\n'){ 
            if (ctx->selStart != ctx->selEnd) delete_selection(ctx);
            grid_insert_char(ctx,L'\n'); 
            grid_after_change(ctx); 
        }
        else if(ch>=0x20 && ch!=0x7F){ 
            if (ctx->selStart != ctx->selEnd) delete_selection(ctx);
            grid_insert_char(ctx,ch); 
            grid_after_change(ctx); 
        }
        return 0; }
    case WM_KEYDOWN: {
        BOOL ctrl=(GetKeyState(VK_CONTROL)&0x8000)!=0;
        BOOL shift=(GetKeyState(VK_SHIFT)&0x8000)!=0;
        int vk=(int)wp;
        
        if(ctrl && (vk=='V'||vk=='v')){ 
            if(!allowEdit) { MessageBeep(0); return 0; }
            grid_paste(ctx); return 0; 
        }
        if(ctrl && (vk=='C'||vk=='c')){ 
            if(!allowSelAndCopy) { MessageBeep(0); return 0; }
            grid_copy_all(ctx); return 0; 
        }
        if(ctrl && (vk=='X'||vk=='x')){ 
            if(!allowSelAndCopy || !allowEdit) { MessageBeep(0); return 0; }
            grid_copy_all(ctx); 
            delete_selection(ctx); grid_after_change(ctx); 
            return 0; 
        }
        if(ctrl && (vk=='A'||vk=='a')){ 
            if(!allowSelAndCopy) { MessageBeep(0); return 0; }
            ctx->selStart = 0; ctx->selEnd = ctx->caret = ctx->buflen;
            grid_set_caret_pos(ctx); InvalidateRect(hwnd,NULL,FALSE);
            return 0; 
        }
        int row,col;
        switch(vk){
            case VK_LEFT:  if(ctx->caret>0)ctx->caret--; break;
            case VK_RIGHT: if(ctx->caret<ctx->buflen)ctx->caret++; break;
            case VK_UP:    index_to_rowcol(ctx,ctx->caret,&row,&col); if(row>0)ctx->caret=rowcol_to_index(ctx,row-1,col); break;
            case VK_DOWN:  index_to_rowcol(ctx,ctx->caret,&row,&col); ctx->caret=rowcol_to_index(ctx,row+1,col); break;
            case VK_HOME:  index_to_rowcol(ctx,ctx->caret,&row,&col); ctx->caret=rowcol_to_index(ctx,row,0); break;
            case VK_END:   index_to_rowcol(ctx,ctx->caret,&row,&col); ctx->caret=end_of_row_index(ctx,row); break;
            case VK_DELETE: 
                if(allowEdit){ 
                    if (ctx->selStart != ctx->selEnd) delete_selection(ctx);
                    else grid_delete(ctx); 
                    grid_after_change(ctx); 
                } else MessageBeep(0); 
                return 0;
            case VK_PRIOR: SendMessageW(hwnd,WM_VSCROLL,SB_PAGEUP,0); return 0;
            case VK_NEXT:  SendMessageW(hwnd,WM_VSCROLL,SB_PAGEDOWN,0); return 0;
            default: return 0;
        }
        
        if (shift && allowSelAndCopy) ctx->selEnd = ctx->caret;
        else ctx->selStart = ctx->selEnd = ctx->caret;
        
        grid_ensure_visible(ctx); grid_set_caret_pos(ctx); InvalidateRect(hwnd,NULL,FALSE);
        return 0; }
    case WM_PAINT: grid_paint(ctx); return 0;
    case WM_ERASEBKGND: return 1;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}

static wchar_t* bytes_to_wide(const unsigned char *data, int n)
{
    if (n<0) n=0;
    if (n>=3 && data[0]==0xEF&&data[1]==0xBB&&data[2]==0xBF){ data+=3;n-=3;
        int wn=MultiByteToWideChar(CP_UTF8,0,(const char*)data,n,NULL,0);
        wchar_t*w=(wchar_t*)malloc((wn+1)*sizeof(wchar_t)); if(!w)return NULL;
        MultiByteToWideChar(CP_UTF8,0,(const char*)data,n,w,wn); w[wn]=0; return w; }
    if (n>=2 && data[0]==0xFF&&data[1]==0xFE){ int cu=(n-2)/2;
        wchar_t*w=(wchar_t*)malloc((cu+1)*sizeof(wchar_t)); if(!w)return NULL;
        memcpy(w,data+2,cu*2); w[cu]=0; return w; }
    if (n>=2 && data[0]==0xFE&&data[1]==0xFF){ int cu=(n-2)/2;
        wchar_t*w=(wchar_t*)malloc((cu+1)*sizeof(wchar_t)); if(!w)return NULL;
        const unsigned char*q=data+2; for(int k=0;k<cu;k++) w[k]=(wchar_t)((q[k*2]<<8)|q[k*2+1]); w[cu]=0; return w; }
    int wn=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,(const char*)data,n,NULL,0);
    if (wn>0){ wchar_t*w=(wchar_t*)malloc((wn+1)*sizeof(wchar_t)); if(!w)return NULL;
        MultiByteToWideChar(CP_UTF8,0,(const char*)data,n,w,wn); w[wn]=0; return w; }
    wn=MultiByteToWideChar(932,0,(const char*)data,n,NULL,0);
    if (wn<=0) wn=MultiByteToWideChar(CP_ACP,0,(const char*)data,n,NULL,0);
    if (wn<=0) return NULL;
    wchar_t*w=(wchar_t*)malloc((wn+1)*sizeof(wchar_t)); if(!w)return NULL;
    if (MultiByteToWideChar(932,0,(const char*)data,n,w,wn)<=0)
        MultiByteToWideChar(CP_ACP,0,(const char*)data,n,w,wn);
    w[wn]=0; return w;
}

static unsigned char* read_file_bytes(const wchar_t *path, int *outLen)
{
    *outLen=0;
    HANDLE h=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    if (h==INVALID_HANDLE_VALUE) return NULL;
    LARGE_INTEGER sz;
    if (!GetFileSizeEx(h,&sz)||sz.QuadPart<=0||sz.QuadPart>128*1024*1024){ CloseHandle(h); return NULL; }
    int n=(int)sz.QuadPart;
    unsigned char*buf=(unsigned char*)malloc(n+1); if(!buf){CloseHandle(h);return NULL;}
    DWORD got=0,total=0;
    while(total<(DWORD)n){ if(!ReadFile(h,buf+total,n-total,&got,NULL)||got==0)break; total+=got; }
    CloseHandle(h); buf[total]=0; *outLen=(int)total; return buf;
}
#ifndef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
#define WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3 0x00002000
#endif
static HINTERNET ses_open(DWORD proxyType)
{
    HINTERNET ses=WinHttpOpen(L"Mozilla/5.0 TaikeiSpeed/1.0",proxyType,
                              WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0);
    if(ses){
        WinHttpSetTimeouts(ses,20000,20000,30000,120000);
        DWORD protos=WINHTTP_FLAG_SECURE_PROTOCOL_TLS1
                    |WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_1
                    |WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2
                    |WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
        WinHttpSetOption(ses,WINHTTP_OPTION_SECURE_PROTOCOLS,&protos,sizeof(protos));
    }
    return ses;
}
static HINTERNET req_send(HINTERNET con,const wchar_t*path,BOOL https,BOOL ignoreCert)
{
    DWORD flags=https?WINHTTP_FLAG_SECURE:0;
    HINTERNET req=WinHttpOpenRequest(con,L"GET",path,NULL,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,flags);
    if(!req) return NULL;
    DWORD redir=WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(req,WINHTTP_OPTION_REDIRECT_POLICY,&redir,sizeof(redir));
    if(ignoreCert && https){
        DWORD sec=SECURITY_FLAG_IGNORE_UNKNOWN_CA|SECURITY_FLAG_IGNORE_CERT_DATE_INVALID
                 |SECURITY_FLAG_IGNORE_CERT_CN_INVALID|SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
        WinHttpSetOption(req,WINHTTP_OPTION_SECURITY_FLAGS,&sec,sizeof(sec));
    }
    if(!WinHttpSendRequest(req,WINHTTP_NO_ADDITIONAL_HEADERS,0,WINHTTP_NO_REQUEST_DATA,0,0,0)
       || !WinHttpReceiveResponse(req,NULL)){
        WinHttpCloseHandle(req); return NULL;
    }
    return req;
}
static unsigned char* http_get_bytes(const wchar_t *url, int *outLen, wchar_t *err, int errcap)
{
    *outLen=0;
    URL_COMPONENTS uc; ZeroMemory(&uc,sizeof uc); uc.dwStructSize=sizeof uc;
    wchar_t host[256]={0}, path[4096]={0};
    uc.lpszHostName=host; uc.dwHostNameLength=255;
    uc.lpszUrlPath=path;  uc.dwUrlPathLength=4095;
    if(!WinHttpCrackUrl(url,0,0,&uc)){ if(err)wcsncpy(err,L"URL の解析に失敗しました。",errcap); return NULL; }
    BOOL https=(uc.nScheme==INTERNET_SCHEME_HTTPS);
    DWORD lastErr=0;
    DWORD proxyTypes[3]={WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                         WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                         WINHTTP_ACCESS_TYPE_NO_PROXY};
    for(int pt=0; pt<3; pt++){
        HINTERNET ses=ses_open(proxyTypes[pt]);
        if(!ses){ lastErr=GetLastError(); continue; }
        HINTERNET con=WinHttpConnect(ses,host,uc.nPort,0);
        if(!con){ lastErr=GetLastError(); WinHttpCloseHandle(ses); continue; }
        HINTERNET req=req_send(con,path,https,FALSE);
        if(!req){ lastErr=GetLastError(); req=req_send(con,path,https,TRUE); }
        if(!req){ if(!lastErr)lastErr=GetLastError(); WinHttpCloseHandle(con); WinHttpCloseHandle(ses); continue; }
        DWORD code=0,csz=sizeof code;
        WinHttpQueryHeaders(req,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX,&code,&csz,WINHTTP_NO_HEADER_INDEX);
        if(code>=400){ WinHttpCloseHandle(req);WinHttpCloseHandle(con);WinHttpCloseHandle(ses);
            if(err)_snwprintf(err,errcap,L"サーバ応答エラー (HTTP %lu)。URL を確認してください。",code);
            return NULL; }
        size_t cap=65536,len=0; unsigned char*buf=(unsigned char*)malloc(cap);
        if(!buf){ WinHttpCloseHandle(req);WinHttpCloseHandle(con);WinHttpCloseHandle(ses); return NULL; }
        for(;;){ DWORD avail=0; if(!WinHttpQueryDataAvailable(req,&avail))break; if(avail==0)break;
            if(len+avail+1>cap){ while(len+avail+1>cap)cap*=2; unsigned char*nb=(unsigned char*)realloc(buf,cap); if(!nb)break; buf=nb; }
            DWORD rd=0; if(!WinHttpReadData(req,buf+len,avail,&rd)||rd==0)break; len+=rd; if(len>128*1024*1024)break; }
        WinHttpCloseHandle(req);WinHttpCloseHandle(con);WinHttpCloseHandle(ses);
        buf[len]=0; *outLen=(int)len; return buf;
    }
    if(err) _snwprintf(err,errcap,
        L"通信に失敗しました (エラー %lu)。\n"
        L"・URLが正しいか、ブラウザで開けるか確認してください\n"
        L"・社内プロキシ/ファイアウォール環境では取得できない場合があります\n"
        L"・PDF/TXT をローカル保存し、ファイルパスを指定する方法もあります", lastErr);
    return NULL;
}
typedef struct { wchar_t url[4200]; unsigned char* result; int len; wchar_t err[512]; } DLJOB;
static DWORD WINAPI dl_thread(LPVOID p){ DLJOB* d=(DLJOB*)p; d->result=http_get_bytes(d->url,&d->len,d->err,511); return 0; }
static unsigned char* http_get_pump(const wchar_t* url, int* outLen, wchar_t* err, int errcap)
{
    *outLen=0;
    DLJOB* d=(DLJOB*)calloc(1,sizeof(DLJOB)); if(!d) return NULL;
    wcsncpy(d->url,url,4199);
    HANDLE th=CreateThread(NULL,0,dl_thread,d,0,NULL);
    if(!th){ unsigned char* r=http_get_bytes(url,outLen,err,errcap); free(d); return r; }
    for(;;){
        DWORD r=MsgWaitForMultipleObjects(1,&th,FALSE,100,QS_ALLINPUT);
        if(r==WAIT_OBJECT_0) break;
        MSG m; while(PeekMessageW(&m,NULL,0,0,PM_REMOVE)){ TranslateMessage(&m); DispatchMessageW(&m); }
    }
    CloseHandle(th);
    unsigned char* res=d->result; *outLen=d->len;
    if(err) wcsncpy(err,d->err,errcap);
    free(d);
    return res;
}

static int run_ps_ocr(const wchar_t* pdfPath, const wchar_t* lang, const wchar_t* outPath)
{
    wchar_t scriptPath[MAX_PATH], tmp[MAX_PATH];
    GetTempPathW(MAX_PATH,tmp);
    _snwprintf(scriptPath,MAX_PATH,L"%staikei_ocr.ps1",tmp);
    HANDLE h=CreateFileW(scriptPath,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
    if(h==INVALID_HANDLE_VALUE) return -1;
    DWORD wr; WriteFile(h,PS_SCRIPT,(DWORD)strlen(PS_SCRIPT),&wr,NULL); CloseHandle(h);

    wchar_t cmd[8192];
    _snwprintf(cmd,8192,
        L"powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File \"%s\" -Pdf \"%s\" -Lang \"%s\" -Out \"%s\"",
        scriptPath,pdfPath,lang,outPath);
    STARTUPINFOW si; ZeroMemory(&si,sizeof si); si.cb=sizeof si;
    si.dwFlags=STARTF_USESHOWWINDOW; si.wShowWindow=SW_HIDE;
    PROCESS_INFORMATION pi; ZeroMemory(&pi,sizeof pi);
    if(!CreateProcessW(NULL,cmd,NULL,NULL,FALSE,CREATE_NO_WINDOW,NULL,NULL,&si,&pi)) return -2;
    for(;;){
        DWORD r=MsgWaitForMultipleObjects(1,&pi.hProcess,FALSE,100,QS_ALLINPUT);
        if(r==WAIT_OBJECT_0) break;
        MSG m; while(PeekMessageW(&m,NULL,0,0,PM_REMOVE)){ TranslateMessage(&m); DispatchMessageW(&m); }
    }
    DWORD code=0; GetExitCodeProcess(pi.hProcess,&code);
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    return (int)code;
}

static void set_status(const wchar_t* s){ SetWindowTextW(g_hStatus, s); }

static void set_ref_box(const wchar_t* w)
{
    g_refGrid.buflen = 0;
    g_refGrid.caret = 0;
    g_refGrid.selStart = 0;
    g_refGrid.selEnd = 0;
    g_refGrid.scrollRow = 0;
    g_refGrid.scrollX = 0;
    for(int i=0; w[i] && g_refGrid.buflen < MAXLEN; i++){
        if(w[i] != L'\r') {
            g_refGrid.buf[g_refGrid.buflen++] = w[i];
        }
    }
    g_refGrid.buf[g_refGrid.buflen] = 0;
    grid_update_scrollbar(&g_refGrid);
    InvalidateRect(g_refGrid.hwnd, NULL, TRUE);
    refresh_reference_from_box();
}

static void do_pdf_ocr(const wchar_t *localPdfPath)
{
    wchar_t outPath[MAX_PATH], tmp[MAX_PATH];
    GetTempPathW(MAX_PATH,tmp);
    _snwprintf(outPath,MAX_PATH,L"%staikei_ocr_out.txt",tmp);
    DeleteFileW(outPath);

    set_status(L"OCR処理中…(描画・レイアウト解析)");
    HCURSOR old=SetCursor(LoadCursor(NULL,IDC_WAIT));
    int code=run_ps_ocr(localPdfPath, L"ja", outPath);
    SetCursor(old);
    set_status(L"待機中");

    int n=0; unsigned char* bytes=read_file_bytes(outPath,&n);
    if(!bytes || n<=0){
        if(bytes)free(bytes);
        if(code<0){
            MessageBoxW(g_hMain,
                L"Failed to launch OCR process (PowerShell).\n"
                L"Check if powershell.exe is available.",
                L"OCR Launch Failed", MB_OK|MB_ICONWARNING);
            return;
        }
        MessageBoxW(g_hMain,
            L"OCR Failed.\n\n"
            L"Possible causes:\n"
            L" - Built-in OCR feature is missing (Windows 10/11 required).\n"
            L" - Rendering timeout.\n"
            L" - PowerShell execution is blocked.\n\n"
            L"Solution: Open the PDF manually and paste the text into the box.",
            L"OCR Failed", MB_OK|MB_ICONWARNING);
        return;
    }
    wchar_t* w=bytes_to_wide(bytes,n); free(bytes);
    if(!w){ MessageBoxW(g_hMain,L"Failed to read OCR result.",L"Error",MB_OK|MB_ICONWARNING); return; }

    if(wcsstr(w,L"__OCR_LANG_UNAVAILABLE__")){
        free(w);
        MessageBoxW(g_hMain,
            L"The required OCR language is not installed.\n\n"
            L"Add the language via Settings > Time & Language > Language,\n"
            L"and install the Optical character recognition feature.\n\n"
            L"Otherwise paste the text into the box manually.",
            L"OCR Language Required", MB_OK|MB_ICONINFORMATION);
        return;
    }
    if(wcsstr(w,L"__OCR_ERROR__")){
        MessageBoxW(g_hMain,w,L"OCR Error",MB_OK|MB_ICONWARNING);
        free(w); return;
    }

    wchar_t* cleaned=clean_ocr_text(w,g_japanese);
    free(w);
    if(!cleaned){ MessageBoxW(g_hMain,L"Cleaning failed.",L"Error",MB_OK|MB_ICONWARNING); return; }
    set_ref_box(cleaned);
    free(cleaned);

    MessageBoxW(g_hMain,
        L"OCR complete. Extracted text may still contain minor errors.\n\n"
        L"WARNING: Review the result against the original PDF and make\n"
        L"manual corrections as scoring depends entirely on this reference.",
        L"OCR Complete (Needs Proofreading)", MB_OK|MB_ICONINFORMATION);
}

static BOOL ends_with_ci(const wchar_t *s, const wchar_t *suf){ size_t ls=wcslen(s),lf=wcslen(suf); if(lf>ls)return FALSE; return _wcsicmp(s+ls-lf,suf)==0; }
static BOOL is_pdf_header(const unsigned char* b, int n){ return n>=4 && b[0]=='%'&&b[1]=='P'&&b[2]=='D'&&b[3]=='F'; }

static void do_load(void)
{
    wchar_t src[4200]; GetWindowTextW(g_hRefPath,src,4199);
    wchar_t *p=src; while(*p==L' '||*p==L'\t')p++;
    size_t L2=wcslen(p); while(L2>0&&(p[L2-1]==L' '||p[L2-1]==L'\t'||p[L2-1]==L'\r'||p[L2-1]==L'\n'))p[--L2]=0;
    if(!*p){
        refresh_reference_from_box();
        if(g_refLoaded) set_status(L"問題文を設定しました。");
        else MessageBoxW(g_hMain,L"Enter a source path/URL or paste directly.",L"No Input",MB_OK|MB_ICONINFORMATION);
        return;
    }
    BOOL isUrl = (_wcsnicmp(p,L"http://",7)==0 || _wcsnicmp(p,L"https://",8)==0);
    BOOL isPdf = ends_with_ci(p,L".pdf");

    if(isUrl){
        wchar_t err[256]={0}; int n=0;
        set_status(L"ダウンロード中…"); HCURSOR old=SetCursor(LoadCursor(NULL,IDC_WAIT));
        unsigned char* bytes=http_get_pump(p,&n,err,255); SetCursor(old); set_status(L"待機中");
        if(!bytes){ MessageBoxW(g_hMain,err[0]?err:L"Download failed.",L"Fetch Failed",MB_OK|MB_ICONWARNING); return; }
        if(!isPdf && is_pdf_header(bytes,n)) isPdf=TRUE;
        if(isPdf){
            wchar_t tmp[MAX_PATH],pdfPath[MAX_PATH]; GetTempPathW(MAX_PATH,tmp);
            _snwprintf(pdfPath,MAX_PATH,L"%staikei_problem.pdf",tmp);
            HANDLE h=CreateFileW(pdfPath,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
            if(h!=INVALID_HANDLE_VALUE){ DWORD wr; WriteFile(h,bytes,n,&wr,NULL); CloseHandle(h); free(bytes); do_pdf_ocr(pdfPath); }
            else { free(bytes); MessageBoxW(g_hMain,L"Temp file creation failed.",L"Error",MB_OK|MB_ICONWARNING); }
        } else {
            wchar_t* w=bytes_to_wide(bytes,n); free(bytes);
            if(w){ 
                wchar_t* cleaned = clean_ocr_text(w, g_japanese);
                if(cleaned) { set_ref_box(cleaned); free(cleaned); set_status(L"URLのテキストを整形して設定しました。"); }
                free(w); 
            }
            else MessageBoxW(g_hMain,L"Text conversion failed.",L"Error",MB_OK|MB_ICONWARNING);
        }
    } else {
        int n=0; unsigned char* bytes=read_file_bytes(p,&n);
        if(!bytes){ MessageBoxW(g_hMain,L"File not found. Check path.",L"Fetch Failed",MB_OK|MB_ICONWARNING); return; }
        if(!isPdf && is_pdf_header(bytes,n)) isPdf=TRUE;
        free(bytes);
        if(isPdf){ do_pdf_ocr(p); }
        else {
            int n2=0; unsigned char* b2=read_file_bytes(p,&n2);
            if(b2){ 
                wchar_t* w=bytes_to_wide(b2,n2); free(b2);
                if(w){ 
                    wchar_t* cleaned = clean_ocr_text(w, g_japanese);
                    if(cleaned) { set_ref_box(cleaned); free(cleaned); set_status(L"ファイルのテキストを整形して設定しました。"); }
                    free(w); 
                }
                else MessageBoxW(g_hMain,L"Text conversion failed.",L"Error",MB_OK|MB_ICONWARNING); }
        }
    }
}

static void do_browse(void)
{
    wchar_t file[MAX_PATH]={0};
    OPENFILENAMEW ofn; ZeroMemory(&ofn,sizeof ofn); ofn.lStructSize=sizeof ofn;
    ofn.hwndOwner=g_hMain; ofn.lpstrFile=file; ofn.nMaxFile=MAX_PATH;
    ofn.lpstrFilter=L"Supported Files (*.pdf;*.txt)\0*.pdf;*.txt\0PDF (*.pdf)\0*.pdf\0Text (*.txt)\0*.txt\0All (*.*)\0*.*\0";
    ofn.Flags=OFN_FILEMUSTEXIST|OFN_HIDEREADONLY;
    if(GetOpenFileNameW(&ofn)){ SetWindowTextW(g_hRefPath,file); }
}

static long lround_d(double x){ return (long)(x<0? x-0.5 : x+0.5); }

static void update_result_full(BOOL stopped)
{
    int total,mist,net; compute_score(&total,&mist,&net);
    double elapsed;
    if(stopped){
        if(g_runStart==0) elapsed=0;
        else { elapsed=(double)(g_stopTick-g_runStart)/1000.0; if(elapsed>RUN_SEC)elapsed=RUN_SEC; if(elapsed<0)elapsed=0; }
    } else elapsed=RUN_SEC;
    int em=(int)elapsed/60, es=(int)elapsed%60;
    const wchar_t* head = stopped? L"=== 採点結果 (途中停止) ===" : L"=== 採点結果 (10分終了) ===";
    const wchar_t* totlbl = g_japanese? L"総文字数" : L"総ストローク";
    const wchar_t* netlbl = g_japanese? L"純字数  " : L"純ストローク";
    const wchar_t* formula= g_japanese? L"(純字数=総字数-誤字数)" : L"(純ストローク=総-誤字×2)";

    wchar_t buf[4096]; int len=0;
    len += _snwprintf(buf+len,4096-len,
        L"%s\r\n言語: %s\r\n経過時間: %d分%02d秒 / 10分\r\n%s: %d\r\n誤字数  : %d\r\n%s: %d  %s\r\n想定級  : %s\r\n",
        head, g_japanese?L"日本語":L"English", em,es, totlbl,total, mist, netlbl,net, formula, grade_label(net,g_japanese));

    if(stopped && elapsed>0.5){
        double f=(double)RUN_SEC/elapsed;
        int pT=(int)lround_d(total*f);
        int pM=(int)lround_d(mist*f);
        int ded=g_japanese? pM : pM*2; if(ded>pT)ded=pT;
        int pN=pT-ded; if(pN<0)pN=0;
        len += _snwprintf(buf+len,4096-len,
            L"\r\n--- 10分時点の想定(現在ペース換算) ---\r\n想定%s: %d\r\n想定誤字数  : %d\r\n想定%s: %d\r\n想定級      : %s\r\n",
            totlbl,pT, pM, netlbl,pN, grade_label(pN,g_japanese));
    } else if(stopped){
        len += _snwprintf(buf+len,4096-len, L"\r\n(準備中の停止のため10分換算は表示しません)\r\n");
    }
    wcsncpy(g_resultText,buf,4095); g_resultText[4095]=0;
    SetWindowTextW(g_hResult,g_resultText);
}

static void update_ui_state(void)
{
    BOOL idle=(g_state==ST_IDLE), prep=(g_state==ST_PREP), run=(g_state==ST_RUNNING);
    BOOL locked = (prep||run);
    EnableWindow(g_hStart, !run && !prep);
    EnableWindow(g_hStop,  run||prep);
    EnableWindow(g_hReset, TRUE);
    EnableWindow(g_hRefPath,!locked); EnableWindow(g_hBrowse,!locked);
    EnableWindow(g_hLoad,!locked); 
    EnableWindow(g_hZen,!locked);
    
    g_inGrid.isEnabled = run;
    g_refGrid.isEnabled = !locked;

    InvalidateRect(g_inGrid.hwnd, NULL, FALSE);
    InvalidateRect(g_refGrid.hwnd, NULL, FALSE);
    
    (void)idle;
}

static void finish_session(int newState)
{
    g_state=newState; g_stopTick=GetTickCount64(); g_inGrid.isEnabled=FALSE;
    KillTimer(g_hMain,ID_TIMEROBJ);
    ShowWindow(g_hOverlay,SW_HIDE);   
    update_ui_state();
    update_result_full(newState==ST_STOPPED);
    SetWindowTextW(g_hTimerLbl, newState==ST_FINISHED? L"終了" : L"停止");
    set_status(newState==ST_FINISHED? L"終了しました" : L"停止しました");
    
    MessageBoxW(g_hMain, g_resultText, L"Result", MB_OK | MB_ICONINFORMATION);
}

static void on_start(void)
{
    refresh_reference_from_box();
    if(!g_refLoaded){
        MessageBoxW(g_hMain,L"先に問題文(基準テキスト)を用意してください。",L"問題文がありません",MB_OK|MB_ICONINFORMATION);
        return;
    }
    g_inGrid.buflen=0; g_inGrid.caret=0; g_inGrid.selStart=0; g_inGrid.selEnd=0; 
    g_inGrid.scrollRow=0; g_inGrid.scrollX=0;
    InvalidateRect(g_inGrid.hwnd,NULL,TRUE);
    g_state=ST_PREP;
    g_phaseEnd=GetTickCount64()+(ULONGLONG)PREP_SEC*1000;
    g_runStart=0; g_lastShownSec=-1; g_scoreDirty=FALSE;
    SetTimer(g_hMain,ID_TIMEROBJ,150,NULL);
    update_ui_state();
    SetWindowTextW(g_hTimerLbl,L"準備 5");
    SetWindowTextW(g_hResult,L"");
    SetWindowTextW(g_hOverlay,L"5");
    SetWindowPos(g_hOverlay,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_SHOWWINDOW);
    set_status(L"まもなく開始します…");
}

static void on_timer(void)
{
    ULONGLONG now=GetTickCount64();
    if(g_state==ST_PREP){
        long long rem=(long long)g_phaseEnd-(long long)now;
        if(rem<=0){
            g_state=ST_RUNNING; g_runStart=now; g_phaseEnd=now+(ULONGLONG)RUN_SEC*1000;
            ShowWindow(g_hOverlay,SW_HIDE);     
            update_ui_state(); SetFocus(g_inGrid.hwnd);
            set_status(L"入力中");
            g_lastShownSec=-1;
        } else {
            int sec=(int)((rem+999)/1000);
            if(sec!=g_lastShownSec){ g_lastShownSec=sec; wchar_t t[32];
                _snwprintf(t,32,L"準備 %d",sec); SetWindowTextW(g_hTimerLbl,t);
                _snwprintf(t,32,L"%d",sec);      SetWindowTextW(g_hOverlay,t); }
        }
    } else if(g_state==ST_RUNNING){
        long long rem=(long long)g_phaseEnd-(long long)now;
        if(rem<=0){ finish_session(ST_FINISHED); return; }
        int sec=(int)(rem/1000);
        if(sec!=g_lastShownSec){ 
            g_lastShownSec=sec; 
            wchar_t t[32]; 
            _snwprintf(t,32,L"%d:%02d",sec/60,sec%60); 
            SetWindowTextW(g_hTimerLbl,t); 
        }
    } else {
        KillTimer(g_hMain,ID_TIMEROBJ);
    }
}

static void on_reset(void)
{
    g_state=ST_IDLE; KillTimer(g_hMain,ID_TIMEROBJ);
    g_inGrid.buflen=0; g_inGrid.caret=0; g_inGrid.selStart=0; g_inGrid.selEnd=0;
    g_inGrid.scrollRow=0; g_inGrid.scrollX=0;
    ShowWindow(g_hOverlay,SW_HIDE);
    SetWindowTextW(g_hTimerLbl,L"--:--");
    SetWindowTextW(g_hResult,L"");
    set_status(L"待機中");
    update_ui_state();
    InvalidateRect(g_inGrid.hwnd,NULL,TRUE);
    grid_update_scrollbar(&g_inGrid);
}

static void copy_result(void)
{
    int len=GetWindowTextLengthW(g_hResult); if(len<=0){ MessageBeep(0); return; }
    wchar_t* buf=(wchar_t*)malloc((len+1)*sizeof(wchar_t)); if(!buf)return;
    GetWindowTextW(g_hResult,buf,len+1);
    HGLOBAL hg=GlobalAlloc(GMEM_MOVEABLE,(len+1)*sizeof(wchar_t));
    if(hg){ wchar_t* d=(wchar_t*)GlobalLock(hg); memcpy(d,buf,(len+1)*sizeof(wchar_t)); GlobalUnlock(hg);
        if(OpenClipboard(g_hMain)){ EmptyClipboard(); SetClipboardData(CF_UNICODETEXT,hg); CloseClipboard(); set_status(L"コピーしました。"); }
        else GlobalFree(hg); }
    free(buf);
}

static HFONT mkfont(int h, const wchar_t* face, BOOL bold)
{
    return CreateFontW(h,0,0,0,bold?FW_BOLD:FW_NORMAL,FALSE,FALSE,FALSE,
        DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,
        DEFAULT_PITCH|FF_DONTCARE, face);
}

static void create_controls(HWND h)
{
    g_fontUI   = mkfont(18,L"Yu Gothic UI",FALSE);
    g_fontTimer= mkfont(48,L"Consolas",TRUE); 
    g_fontMono = mkfont(17,L"MS Gothic",FALSE);
    g_fontHuge = mkfont(300,L"Yu Gothic UI",TRUE);  
    g_brOverlay= CreateSolidBrush(RGB(18,22,34));
    
    g_fontGridIn  = mkfont(21,L"MS Gothic",FALSE);
    g_fontGridRef = mkfont(21,L"MS Gothic",FALSE);

    CreateWindowExW(0,L"STATIC",L"取得元(.txt / URL / PDF):",WS_CHILD|WS_VISIBLE,
        10,12,210,22,h,(HMENU)-1,g_hInst,NULL);
    g_hRefPath=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL,
        225,9,520,26,h,(HMENU)ID_REFPATH,g_hInst,NULL);
    g_hBrowse=CreateWindowExW(0,L"BUTTON",L"参照…",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,
        750,9,70,26,h,(HMENU)ID_BROWSE,g_hInst,NULL);
    g_hLoad=CreateWindowExW(0,L"BUTTON",L"読込(OCR)",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,
        826,9,110,26,h,(HMENU)ID_LOAD,g_hInst,NULL);

    g_lblRef=CreateWindowExW(0,L"STATIC",L"問題文(模範解答) ※OCRは要校正／改行は採点対象外",
        WS_CHILD|WS_VISIBLE, 10,44,920,18,h,(HMENU)-1,g_hInst,NULL);
    g_hZen=CreateWindowExW(0,L"BUTTON",L"数字を全角に",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,
        936,42,96,22,h,(HMENU)ID_ZEN,g_hInst,NULL);

    g_hStatus=CreateWindowExW(0,L"STATIC",L"待機中",WS_CHILD|WS_VISIBLE,
        10,196,470,26,h,(HMENU)-1,g_hInst,NULL);
    g_hTimerLbl=CreateWindowExW(0,L"STATIC",L"--:--",WS_CHILD|WS_VISIBLE|SS_CENTER,
        486,192,150,32,h,(HMENU)-1,g_hInst,NULL);
    g_hStart=CreateWindowExW(0,L"BUTTON",L"タイマー開始(10分)",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,
        648,192,180,32,h,(HMENU)ID_START,g_hInst,NULL);
    g_hStop=CreateWindowExW(0,L"BUTTON",L"途中停止",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,
        834,192,100,32,h,(HMENU)ID_STOP,g_hInst,NULL);
    g_hReset=CreateWindowExW(0,L"BUTTON",L"リセット",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,
        940,192,92,32,h,(HMENU)ID_RESET,g_hInst,NULL);

    g_lblGrid=CreateWindowExW(0,L"STATIC",L"入力欄(40字/行・全角半角とも1マス・Ctrl+Vで貼付)",
        WS_CHILD|WS_VISIBLE, 10,230,1022,18,h,(HMENU)-1,g_hInst,NULL);

    WNDCLASSW wc; ZeroMemory(&wc,sizeof wc);
    wc.lpfnWndProc=GridProc; wc.hInstance=g_hInst; wc.hCursor=LoadCursor(NULL,IDC_IBEAM);
    wc.hbrBackground=NULL; wc.lpszClassName=L"TaikeiGridClass";
    RegisterClassW(&wc);

    HWND hRef = CreateWindowExW(WS_EX_CLIENTEDGE,L"TaikeiGridClass",L"",
        WS_CHILD|WS_VISIBLE|WS_VSCROLL|WS_HSCROLL|WS_TABSTOP,
        10,64,1022,120,h,(HMENU)ID_REFTEXT,g_hInst,NULL);
    SetWindowLongPtrW(hRef, GWLP_USERDATA, (LONG_PTR)&g_refGrid);
    g_refGrid.hwnd = hRef;
    g_refGrid.cellW = 23;
    g_refGrid.cellH = 32;
    g_refGrid.hFont = g_fontGridRef;

    HWND hIn = CreateWindowExW(WS_EX_CLIENTEDGE,L"TaikeiGridClass",L"",
        WS_CHILD|WS_VISIBLE|WS_VSCROLL|WS_HSCROLL|WS_TABSTOP,
        10,250,1022,360,h,(HMENU)ID_GRID,g_hInst,NULL);
    SetWindowLongPtrW(hIn, GWLP_USERDATA, (LONG_PTR)&g_inGrid);
    g_inGrid.hwnd = hIn;
    g_inGrid.cellW = 23;
    g_inGrid.cellH = 32;
    g_inGrid.hFont = g_fontGridIn;

    g_hOverlay=CreateWindowExW(0,L"STATIC",L"",
        WS_CHILD|SS_CENTER|SS_CENTERIMAGE,
        10,250,1022,360,h,(HMENU)-1,g_hInst,NULL);
    SendMessageW(g_hOverlay,WM_SETFONT,(WPARAM)g_fontHuge,TRUE);

    g_lblResult=CreateWindowExW(0,L"STATIC",L"採点結果",WS_CHILD|WS_VISIBLE,10,616,200,18,h,(HMENU)-1,g_hInst,NULL);
    g_hCopy=CreateWindowExW(0,L"BUTTON",L"結果をコピー",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,
        916,614,116,24,h,(HMENU)ID_COPY,g_hInst,NULL);
    g_hResult=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",
        WS_CHILD|WS_VISIBLE|WS_VSCROLL|ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL,
        10,638,1022,150,h,(HMENU)ID_RESULT,g_hInst,NULL);

    g_lblDisclaimer=CreateWindowExW(0,L"STATIC",
        L"【免責事項】本ツールは個人が作成した非公式な練習用ツールであり、日本情報処理検定協会とは一切関係がありません。問題データはご自身で用意したものを使用してください。",
        WS_CHILD|WS_VISIBLE, 10,800,1022,18,h,(HMENU)-1,g_hInst,NULL);

    HWND ctrls[]={g_hRefPath,g_hBrowse,g_hLoad,g_hZen,g_hStatus,g_hStart,g_hStop,g_hReset,g_hCopy};
    for(int i=0;i<(int)(sizeof ctrls/sizeof ctrls[0]);i++) SendMessageW(ctrls[i],WM_SETFONT,(WPARAM)g_fontUI,TRUE);
    SendMessageW(g_hResult,WM_SETFONT,(WPARAM)g_fontMono,TRUE);
    SendMessageW(g_hTimerLbl,WM_SETFONT,(WPARAM)g_fontTimer,TRUE);
    for(HWND c=GetWindow(h,GW_CHILD); c; c=GetWindow(c,GW_HWNDNEXT)){
        wchar_t cls[32]; GetClassNameW(c,cls,32);
        if(_wcsicmp(cls,L"STATIC")==0 && c != g_hTimerLbl && c != g_hOverlay) SendMessageW(c,WM_SETFONT,(WPARAM)g_fontUI,TRUE);
    }
}

static void layout_controls(HWND h)
{
    RECT rc; GetClientRect(h,&rc);
    int W=rc.right, H=rc.bottom, M=10, gap=8;
    int right=W-M;

    int loadW=110, browseW=70, g2=6;
    int xLoad=right-loadW; int xBrowse=xLoad-g2-browseW;
    int xPathL=224, xPathR=xBrowse-g2; int pathW=xPathR-xPathL; if(pathW<120)pathW=120;
    MoveWindow(g_hRefPath,xPathL,9,pathW,26,TRUE);
    MoveWindow(g_hBrowse,xBrowse,9,browseW,26,TRUE);
    MoveWindow(g_hLoad,xLoad,9,loadW,26,TRUE);

    int y1=44;
    int btnW=176, stopW=96, resetW=88, timerW=180, bg=6; 
    int xReset=right-resetW; int xStop=xReset-bg-stopW; int xStart=xStop-bg-btnW; int xTimer=xStart-bg-timerW;
    int statusW=xTimer-M-bg; if(statusW<120)statusW=120;
    MoveWindow(g_hStatus,M,y1+3,statusW,26,TRUE);
    MoveWindow(g_hTimerLbl,xTimer,y1-8,timerW,48,TRUE); 
    MoveWindow(g_hStart,xStart,y1,btnW,32,TRUE);
    MoveWindow(g_hStop,xStop,y1,stopW,32,TRUE);
    MoveWindow(g_hReset,xReset,y1,resetW,32,TRUE);

    int resultH=140, copyH=24, lblH=18, discH=18;
    int yDisc=H-M-discH;
    int yRes=yDisc-M-resultH;
    int yResLbl=yRes-lblH-2;
    MoveWindow(g_lblDisclaimer,M,yDisc,W-2*M,discH,TRUE);
    MoveWindow(g_hResult,M,yRes,W-2*M,resultH,TRUE);
    MoveWindow(g_lblResult,M,yResLbl,200,lblH,TRUE);
    MoveWindow(g_hCopy,right-120,yResLbl-2,120,copyH,TRUE);

    int splitTop=y1+40;
    int paneTop=splitTop+lblH+2;
    int paneBottom=yResLbl-8;
    int paneH=paneBottom-paneTop; if(paneH<120)paneH=120;
    int innerW=W-2*M-gap;
    int leftW=(innerW)/2; if(leftW<260)leftW=260;
    int rightW=innerW-leftW; if(rightW<200)rightW=200;
    int leftX=M, rightX=M+leftW+gap;

    int zenW=104;
    int refLblW=leftW-zenW-6; if(refLblW<80)refLblW=80;
    MoveWindow(g_lblRef,leftX,splitTop,refLblW,lblH,TRUE);
    MoveWindow(g_hZen,leftX+leftW-zenW,splitTop-2,zenW,22,TRUE);
    MoveWindow(g_refGrid.hwnd,leftX,paneTop,leftW,paneH,TRUE);

    MoveWindow(g_lblGrid,rightX,splitTop,rightW,lblH,TRUE);
    MoveWindow(g_inGrid.hwnd,rightX,paneTop,rightW,paneH,TRUE);
    MoveWindow(g_hOverlay,rightX,paneTop,rightW,paneH,TRUE);

    InvalidateRect(h,NULL,TRUE);
}

static LRESULT CALLBACK MainProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch(msg){
    case WM_CREATE:
        g_hMain=h; create_controls(h); update_ui_state();
        SetWindowTextW(g_hResult,
            L"本ソフトは、日本情報処理検定協会の「文章入力スピード認定試験(日本語)」向けのサードパーティ製練習ツール(非公式)です。\r\n"
            L"問題文データは利用者自身で用意(所有・入手)したものを使用してください。ツール自体には問題データを含みません。\r\n\r\n"
            L"使い方:\r\n"
            L" 1) 取得元に問題PDF(またはURL)を指定し『読込(OCR)』を押すと、画像化→文字起こしして『問題文』欄に入ります。\r\n"
            L"    (.txt や テキストURL も可。直接貼り付けることも可能です。)\r\n"
            L"    ※ OCRは誤認識があるため必ずPDFと照合して校正してください。\r\n"
            L" 2) 『タイマー開始(10分)』→ 5秒カウントダウン → 入力開始 → 10分で自動終了。\r\n"
            L" 3) 途中で『途中停止』を押すと、その時点の結果＋10分換算の想定を表示します。");
        return 0;
    case WM_SIZE: layout_controls(h); return 0;
    case WM_GETMINMAXINFO: { MINMAXINFO* mmi=(MINMAXINFO*)lp; mmi->ptMinTrackSize.x=820; mmi->ptMinTrackSize.y=640; return 0; }
    case WM_TIMER: if(wp==ID_TIMEROBJ) on_timer(); return 0;
    case WM_CTLCOLORSTATIC:
        if((HWND)lp==g_hOverlay){
            HDC dc=(HDC)wp;
            SetBkColor(dc, RGB(18,22,34));
            SetTextColor(dc, RGB(255,212,64));
            return (LRESULT)g_brOverlay;
        }
        if((HWND)lp==g_hTimerLbl){
            HDC dc=(HDC)wp;
            if(g_state==ST_RUNNING){
                ULONGLONG now=GetTickCount64();
                long long rem=(long long)g_phaseEnd-(long long)now;
                if(rem > 0 && rem <= 60000){ 
                    SetTextColor(dc, RGB(255,0,0));
                } else {
                    SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
                }
            } else {
                SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
            }
            SetBkColor(dc, GetSysColor(COLOR_BTNFACE));
            return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
        }
        return DefWindowProcW(h,msg,wp,lp);
    case WM_COMMAND: {
        int id=LOWORD(wp);
        if(id==ID_LOAD)  do_load();
        else if(id==ID_BROWSE) do_browse();
        else if(id==ID_START) on_start();
        else if(id==ID_STOP){ if(g_state==ST_RUNNING||g_state==ST_PREP) finish_session(ST_STOPPED); }
        else if(id==ID_RESET) on_reset();
        else if(id==ID_COPY) copy_result();
        else if(id==ID_ZEN){ zenkakuify_box(); set_status(L"問題文の数字を全角に変換しました。"); }
        return 0; }
    case WM_DESTROY: if(g_brOverlay){ DeleteObject(g_brOverlay); g_brOverlay=NULL; } PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h,msg,wp,lp);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE hPrev, PWSTR cmd, int show)
{
    (void)hPrev;(void)cmd;
    g_hInst=hInst;
    INITCOMMONCONTROLSEX icc={sizeof icc, ICC_STANDARD_CLASSES}; InitCommonControlsEx(&icc);
    WNDCLASSW wc; ZeroMemory(&wc,sizeof wc);
    wc.lpfnWndProc=MainProc; wc.hInstance=hInst;
    wc.hCursor=LoadCursor(NULL,IDC_ARROW);
    wc.hbrBackground=(HBRUSH)(COLOR_BTNFACE+1);
    wc.lpszClassName=L"TaikeiSpeedMain"; 
    wc.hIcon=LoadIcon(NULL,IDI_APPLICATION);
    RegisterClassW(&wc);

    HWND h=CreateWindowExW(0,L"TaikeiSpeedMain",
        L"文章入力スピード 練習ツール (非公式サードパーティ製)",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,CW_USEDEFAULT,1900,1000,
        NULL,NULL,hInst,NULL);
    if(!h) return 1;
    ShowWindow(h,show); UpdateWindow(h);

    MSG msg;
    while(GetMessageW(&msg,NULL,0,0)>0){
        if(!IsDialogMessageW(h,&msg)){ TranslateMessage(&msg); DispatchMessageW(&msg); }
    }
    return (int)msg.wParam;
}