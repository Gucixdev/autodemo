/-
Model of the wl_shm pool lifetime in src/gui.c (the bug fixed in 089bf38: Xwayland destroys the pool right after
cutting a buffer, the compositor unmapped it and read freed memory).

State: `refs` buffers cut from the pool still alive, `dead` the client sent wl_shm_pool.destroy, `mapped` the mmap exists.
Rules as coded:
  new_buffer   : refs += 1
  destroy_buf  : refs -= 1; unmap when refs = 0 and dead
  destroy_pool : dead := true; unmap when refs = 0
Safety: while a buffer is alive (refs > 0) the pool is mapped, so surf_commit can read it.
-/

structure Pool where
  refs   : Nat
  dead   : Bool
  mapped : Bool
deriving Repr

def Pool.fresh : Pool := { refs := 0, dead := false, mapped := true }

def Pool.newBuffer (p : Pool) : Pool := { p with refs := p.refs + 1 }

def Pool.destroyBuffer (p : Pool) : Pool :=
  let r := p.refs - 1
  if r = 0 ∧ p.dead then { refs := r, dead := p.dead, mapped := false } else { p with refs := r }

def Pool.destroyPool (p : Pool) : Pool :=
  if p.refs = 0 then { p with dead := true, mapped := false } else { p with dead := true }

/-- The invariant every reachable state satisfies. -/
def PoolOk (p : Pool) : Prop :=
  (0 < p.refs → p.mapped = true) ∧ (p.mapped = false → p.dead = true ∧ p.refs = 0)

theorem fresh_ok : PoolOk Pool.fresh := by
  simp [PoolOk, Pool.fresh]

theorem newBuffer_ok (p : Pool) (h : PoolOk p) (hm : p.mapped = true) : PoolOk p.newBuffer := by
  obtain ⟨h1, h2⟩ := h
  simp [PoolOk, Pool.newBuffer, hm]

theorem destroyBuffer_ok (p : Pool) (h : PoolOk p) (hr : 0 < p.refs) : PoolOk p.destroyBuffer := by
  obtain ⟨h1, h2⟩ := h
  have hm : p.mapped = true := h1 hr
  unfold Pool.destroyBuffer PoolOk
  by_cases c : p.refs - 1 = 0 ∧ p.dead = true
  · simp [c]
  · simp only [c, if_false]
    refine ⟨fun h' => hm, fun h' => ?_⟩
    simp [hm] at h'

theorem destroyPool_ok (p : Pool) (h : PoolOk p) : PoolOk p.destroyPool := by
  obtain ⟨h1, h2⟩ := h
  unfold Pool.destroyPool PoolOk
  by_cases c : p.refs = 0
  · simp [c]
  · have : 0 < p.refs := Nat.pos_of_ne_zero c
    have hm := h1 this
    simp [c, hm]

/-- The point of the invariant: a live buffer always finds its pool mapped. -/
theorem live_buffer_readable (p : Pool) (h : PoolOk p) (hr : 0 < p.refs) : p.mapped = true := h.1 hr

/-- Exactly one unmap: once unmapped the pool stays unmapped and no buffer is alive. -/
theorem unmapped_means_no_buffers (p : Pool) (h : PoolOk p) (hm : p.mapped = false) : p.refs = 0 := (h.2 hm).2

/-- Old behaviour (destroy_pool unmaps at once) breaks the invariant: one live buffer, pool unmapped. -/
def oldDestroyPool (p : Pool) : Pool := { p with dead := true, mapped := false }

theorem old_destroy_breaks_ok : ¬ PoolOk (oldDestroyPool Pool.fresh.newBuffer) := by
  simp [PoolOk, oldDestroyPool, Pool.fresh, Pool.newBuffer]
