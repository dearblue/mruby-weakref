#!ruby

assert "WeakRef" do
  src = "abcdefg"
  ref = WeakRef.new(src)

  assert_equal src.inspect, ref.inspect

  GC.start
  GC.disable

  ref = WeakRef.new(src.dup)

  assert_equal src.inspect, ref.inspect

  GC.enable
  GC.start

  assert_raise(WeakRef::RefError) { ref.inspect }

  # more test!
end
