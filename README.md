# C ノードネットワーク

これはCで実装されたP2Pノードネットワークで、複数のノードがそれぞれのノードIDを使って互いに通信することができます。NAT越えの機能を備えており、異なるネットワーク上のコンピュータ間でも自動的に通信が可能です。

## 特徴

- 最大100個のノードを作成し、それぞれに一意のIDを割り当て
- 各ノードは独自のスレッドで実行
- ノードは受信者のIDを使用して他のノードにメッセージを送信可能
- メッセージには送信者ID、受信者ID、メッセージデータが含まれる
- すべてのノードはメッシュネットワークで相互に接続
- **NAT越え機能によりインターネット越しの通信が可能（デフォルトで有効）**
- **自動ピア発見機能によりネットワーク上の他のノードを自動的に検出（デフォルトで有効）**
- **UPnPによるポート転送の自動設定（デフォルトで有効）**
- **ファイアウォール対策機能により一般的に許可されているポートを使用**
- **接続の信頼性向上のための自動再接続機能**
- **簡易的なメッセージ認証機能**
- **ネットワーク診断機能によるトラブルシューティング**
- **対話的なコマンドラインインターフェース**

## 実装の詳細

- 通信にUDPソケットを使用
- 各ノードは異なるポート（BASE_PORT + node_id）でリッスン
- メッセージは一つのノードから別のノードに直接送信
- スレッドを使用して非同期でメッセージを受信
- STUNサーバーを使用してNAT越えを実現
- UPnPを使用してルーターのポート転送を自動設定
- マルチキャストを使用したピア発見
- ピア情報の共有によるネットワークの拡大
- 定期的なキープアライブメッセージによる接続維持
- 複数のポートを試行するファイアウォール対策
- 簡易的なHMACによるメッセージ認証

## ビルドと実行方法

プロジェクトをビルドするには：

```bash
make
```

ネットワークを実行するには：

```bash
./node_network [オプション]
```

オプション：
- `-n COUNT` - 作成するノードの数（デフォルト：10）
- `-T` - NAT越え機能を無効化（デフォルトでは有効）
- `-U` - UPnPポート転送を無効化（デフォルトでは有効）
- `-D` - 自動ピア発見を無効化（デフォルトでは有効）
- `-F` - ファイアウォール対策モードを無効化（デフォルトでは有効）
- `-s SERVER` - 使用するSTUNサーバー（デフォルト：stun.l.google.com）
- `-p PEER` - リモートピアを追加（形式：id:ip:port）
- `-h` - ヘルプメッセージを表示

プログラムは以下を行います：
1. 指定された数のノードを作成
2. NAT越え機能が有効な場合、公開IPアドレスとポートを検出
3. UPnPが有効な場合、ルーターのポート転送を設定
4. 自動ピア発見が有効な場合、ネットワーク上の他のノードを検出
5. 各ノードが他のすべてのノードにメッセージを送信するデモを実行
6. 対話的なコマンドラインインターフェースを提供
7. 中断（Ctrl+C）されるまで実行を継続

## 対話的なコマンド

プログラム実行中に以下のコマンドを使用できます：

- `status` - すべてのノードの状態を表示
- `list` または `nodes` - すべてのノードとピアを一覧表示
- `ping <id>` - 特定のノードにpingを送信
- `send <id> <message>` - 特定のノードにメッセージを送信
- `diag` または `diagnostics` - ネットワーク診断を実行
- `help` - ヘルプメッセージを表示
- `exit` または `quit` - プログラムを終了

## 異なるコンピュータ間での通信

異なるコンピュータ間で通信するには、以下の手順に従います：

### 自動ピア発見を使用する場合（デフォルト）

1. 各コンピュータでプログラムをビルド
2. 最初のコンピュータでノードを起動：`./node_network -n 5`
3. 2台目のコンピュータでノードを起動：`./node_network -n 5`

同じローカルネットワーク上にある場合、ノードは自動的に互いを発見して接続します。

### NAT越えを使用する場合（デフォルト）

1. 各コンピュータでプログラムをビルド
2. 最初のコンピュータでノードを起動：`./node_network -n 5`
3. 最初のコンピュータの公開IPアドレスとポートをメモ（出力に表示されます）
4. 2台目のコンピュータでノードを起動：`./node_network -n 5 -p 0:公開IP:公開ポート`

### ファイアウォールがある場合

ファイアウォール対策モードはデフォルトで有効になっています。これにより、一般的に許可されているポート（80, 443など）を使用してファイアウォールを通過します。

無効にする場合は、`-F`オプションを使用します：

```bash
./node_network -n 5 -F
```

## コード構造

- `node.h/node.c` - ノード関数（作成、破棄、接続、送信、受信）の実装
- `stun.h/stun.c` - STUNクライアントの実装（NAT越え用）
- `upnp.h/upnp.c` - UPnPクライアントの実装（ポート転送用）
- `discovery.h/discovery.c` - ピア発見機能の実装
- `nat_traversal.c` - NAT越え関連の機能の実装
- `firewall.h/firewall.c` - ファイアウォール対策機能の実装
- `reliability.h/reliability.c` - 接続の信頼性向上機能の実装
- `security.h/security.c` - メッセージ認証機能の実装
- `diagnostics.h/diagnostics.c` - ネットワーク診断機能の実装
- `main.c` - ネットワークをセットアップしメッセージングをデモするメインプログラム
- `Makefile` - ビルド設定

## プロジェクトの拡張

この実装は以下のようにさまざまな方法で拡張できます：
- より複雑なネットワークトポロジのためのルーティング機能の追加
- 完全な信頼性メカニズム（確認応答、再送信）の実装
- 強力な暗号化の追加（OpenSSLなどを使用）
- 分散ハッシュテーブル（DHT）の実装
- アプリケーション固有のメッセージタイプとハンドラの追加
- WebRTCのようなより高度なNAT越え技術の実装
- GUIの追加