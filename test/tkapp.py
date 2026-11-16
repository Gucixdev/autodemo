import os
import tkinter as tk

def clicked():
    r.title("tk:clicked")
    m = os.environ.get("TK_MARK")
    if m:
        open(m, "w").write("clicked\n")
r = tk.Tk(); r.title("tk:ready"); r.geometry("400x240+0+0")
tk.Label(r, text="Hello from Tk over Xwayland", font=("TkDefaultFont", 14)).pack(pady=10)
c = tk.Canvas(r, width=360, height=120, bg="white", highlightthickness=0); c.pack()
c.create_rectangle(10, 10, 170, 110, fill="#d33", outline=""); c.create_rectangle(190, 10, 350, 110, fill="#3a3", outline="")
tk.Button(r, text="click me", command=clicked).pack(pady=8)
r.mainloop()
