# 文章入力スピード 練習ツール (非公式サードパーティ製)

このソフトウェアは、日本情報処理検定協会が主催する「文章入力スピード認定試験（日本語）」の練習を目的として個人が作成した、**非公式のサードパーティ製アプリケーション**です。
Windows環境（Win32 API / C言語）で動作するデスクトップアプリケーションとして設計されています。

**【重要・免責事項】**

* 本ツールは個人が独自に開発したものであり、日本情報処理検定協会とは一切関係がありません。

* 本ツールには練習用の**問題文データ（過去問など）は一切含まれていません**。利用者は、自身で正当に入手・所有している問題データ（PDF、テキストファイル等）を用意し、自身の責任において使用してください。

## 主な機能

* **PDF/テキストの読み込みと自動OCR**

  * 問題文のPDFファイルを指定すると、Windows内蔵のOCR機能（Windows 10/11対応）を利用して自動的にテキスト化し、問題文としてセットします。

  * ローカルのテキストファイル（.txt）や、Web上のテキストURLの読み込みにも対応しています。

* **日本語入力スピードの採点（シミュレーション）**

  * 本番同様の「10分間」のタイマー機能を備えています（残り1分を切ると赤字で強調表示されます）。

  * 入力したテキストを基準テキスト（問題文）と照合し、「総字数」「誤字数」「純字数」を自動計算します。

  * 10分間終了時にポップアップで結果を表示し、純字数に基づいた想定級（特段〜6級）を判定します。

* **途中停止時のペース換算**

  * 10分経過前に途中でタイマーを停止した場合、その時点の入力ペースから「10分間入力し続けた場合の想定結果」を算出して表示します。

* **全角数字一括変換機能**

  * 読み込んだ問題文に含まれる半角数字を、ボタン一つで全角数字に変換できます。

## 採点基準について（日本語）

本ツールでは、以下の計算式と基準を用いて採点シミュレーションを行います。

* **計算式:** `純字数 = 総字数 - 誤字数` (1ミスにつき1文字減点)

* **想定級の基準（純字数）:**

  * 特段：2,000文字以上

  * 初段：1,500文字以上

  * 1級：1,000文字以上

  * 準1級：800文字以上

  * 2級：600文字以上

  * 準2級：450文字以上

  * 3級：350文字以上

  * 4級：250文字以上

  * 5級：100文字以上

  * 6級：50文字以上

## コンパイル方法

本ツールはC言語で記述されています。以下のいずれかのコンパイラを使用してビルドしてください。

### MinGW-w64 を使用する場合

```
x86_64-w64-mingw32-gcc taikei_speed.c -o taikei_speed.exe -municode -mwindows -O2 -lwinhttp -lcomctl32 -lgdi32 -lcomdlg32 -lshell32 -limm32

```

### MSVC (Visual Studio x64 Native Tools コマンドプロンプト) を使用する場合

```
cl /utf-8 /DUNICODE /D_UNICODE taikei_speed.c user32.lib gdi32.lib comdlg32.lib winhttp.lib comctl32.lib shell32.lib imm32.lib /link /SUBSYSTEM:WINDOWS

```

## 使い方

1. **起動**
   コンパイルした `taikei_speed.exe` を実行します。

2. **問題文のセット**

   * **ファイルから読み込む:** 「取得元」の入力欄にPDFファイルやテキストファイルのパスを入力（または「参照…」ボタンから選択）し、「読込(OCR)」ボタンを押します。

   * **URLから読み込む:** テキストデータやPDFのURLを直接入力し、「読込(OCR)」ボタンを押します。

   * **手動で貼り付ける:** 左側の「問題文(模範解答)」のテキストエリアに、直接テキストを貼り付けることも可能です。

   * *(注意)* OCR機能は誤認識を起こす可能性があります。読み込み後は、必ず元のPDF等と見比べ、誤字脱字を手動で修正（校正）してください。この問題文が採点の基準となります。

3. **入力の開始**
   「タイマー開始(10分)」ボタンを押します。
   画面全体に大きく「5」からカウントダウンが表示され、ゼロになると右側の入力欄に入力できるようになります。

4. **入力中**
   右側の入力欄に、左側の問題文を見ながらテキストを入力してください。（改行は採点ロジックでは無視され、文字の並びで判定されます）。
   残り時間が1分を切ると、タイマーの文字が赤色に変わります。

5. **終了と結果確認**

   * 10分経過すると自動的に入力がロックされ、最終結果がポップアップ画面で表示されます。

   * 途中でやめたい場合は、「途中停止」ボタンを押してください。その時点での結果と、10分換算での予想結果が表示されます。

   * 結果は画面下部の「採点結果」欄にも表示され、「結果をコピー」ボタンでクリップボードにコピーできます。

6. **リセット**
   「リセット」ボタンを押すと、入力した内容とタイマーがクリアされ、最初の状態に戻ります（設定した問題文は保持されます）。

## 動作環境

* Windows 10 または Windows 11

* （OCR機能を利用する場合）Windowsの言語設定にて「日本語」の「光学式文字認識 (OCR)」機能がインストールされていること。

## ライセンス

本ソフトウェアは **MIT License** のもとで公開されています。
自己責任において、商用・非商用問わず自由に利用、改変、再配布することが可能です。

```
MIT License

Copyright (c) 2026

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

```

---

### ☕COFFEE
投げ銭してほしい本心です。イーロンマスク待ってます
```
BTC: bc1q7vu8d6nmsdmswldzq09thqxm9vhnjqlyehedx9

LTC: LVy59XzBwvdfMuf5khGmHKNiQUSF3oi6sa

ETH: 0xBbF239FBaea5F3aB3862435af376594b32cF55Da
```
---

制作者連絡先:
discord `horirofu`

<small>このReadmeは制作者の怠惰によりAIで生成されています。</small>
