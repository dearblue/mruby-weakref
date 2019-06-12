class WeakRef
  def initialize(target)
    ::WeakRef.__initialize_reference__ self, target
    super
  end
end
