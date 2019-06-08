# mruby-weakref

mruby 上で利用可能な、弱い参照オブジェクトです。

CRuby の標準添付ライブラリである `weakref` を模倣しているつもりです。

[How to weakly reference a mrb_value in C++ #4479](https://github.com/mruby/mruby/issues/4479) に触発されて作成しました。


## 注意と制限事項


## くみこみかた

`build_config.rb` に gem として追加して、mruby をビルドして下さい。

```ruby
MRuby::Build.new do |conf|
  conf.gem "mruby-weakref", github: "dearblue/mruby-weakref"
end
```

- - - -

mruby gem パッケージとして依存したい場合、`mrbgem.rake` に記述して下さい。

```ruby
# mrbgem.rake
MRuby::Gem::Specification.new("mruby-XXX") do |spec|
  ...
  spec.add_dependency "mruby-weakref", github: "dearblue/mruby-weakref"
end
```


## つかいかた

たぶん <https://docs.ruby-lang.org/ja/latest/library/weakref.html> の通りに動作します……するといいな。

  - サンプルコード (`example.rb`):

    ```ruby
    def makeref
      target = [*"A".."Z"]
      ref = WeakRef.new(target)
      puts ref.inspect
      ref
    end

    ref = makeref
    p ref.weakref_alive?
    20.times { GC.start }
    p ref.weakref_alive?
    p ref.__getobj__
    ```

  - mruby で実行:

    ```
    % ./build/host/bin/mruby example.rb
    ["A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z"]
    true
    nil
    trace (most recent call last):
            [0] example.rb:12
    example.rb:12: Invalid Reference - probably recycled (WeakRef::RefError)
    ```

  - ruby-2.6.3 で実行:

    ```
    % ruby26 -r weakref example.rb
    ["A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z"]
    true
    nil
    Traceback (most recent call last):
    example.rb: Invalid Reference - probably recycled (WeakRef::RefError)
    ```


## 実装方法について

実装方法については [HOW_TO_IMP.md](HOW_TO_IMP.md) を見て下さい。


## Specification

  - Package name: mruby-weakref
  - Version: 0.1
  - Product quality: CONCEPT
  - Author: [dearblue](https://github.com/dearblue)
  - Project page: <https://github.com/dearblue/mruby-weakref>
  - Licensing: [Creative Commons Zero License (CC0; likely Public Domain)](LICENSE)
  - Required mruby version: 1.4.0 or later
  - Object code size: About 4 Ki bytes on FreeBSD 12.0 AMD64 with clang
  - Heap usages per one weakref instance on FreeBSD 12.0 AMD64 (rough estimate for string object):
      - 48 bytes: weakref object
      - 20×N bytes: delegated method table (N is count of string methods (probably more than 160))
      - 48 + 32 bytes: capture object
      - 48 bytes: singleton class
      - 144 bytes (?): instance variable table
      - 48 bytes: array for back reference
      - 3500+ bytes: total
  - Dependency external mrbgems:
      - [mruby-delegate](https://github.com/dearblue/mruby-delegate)
  - Bundled C libraries (git-submodules): (NONE)
