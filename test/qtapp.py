# Qt test client for the gui backend (PySide6, QT_QPA_PLATFORM=wayland). Every interaction shows up in the window
# title, so a demo can assert it with `WaitFor title "..."`. Widgets: button, combo box (popup), menu (popup), modal dialog.
import sys
from PySide6.QtWidgets import (QApplication, QMainWindow, QPushButton, QComboBox, QWidget, QVBoxLayout, QDialog, QLineEdit)

app = QApplication(sys.argv)
w = QMainWindow(); w.setWindowTitle("qt:ready")
box = QWidget(); lay = QVBoxLayout(box); w.setCentralWidget(box)
btn = QPushButton("Click me"); btn.setToolTip("a tooltip popup")
btn.setStyleSheet("background:#c33;color:white;font-size:22px;padding:18px")
combo = QComboBox(); combo.addItems(["alpha", "beta", "gamma"]); combo.setStyleSheet("font-size:20px")
dlgbtn = QPushButton("Dialog"); dlgbtn.setStyleSheet("font-size:20px")
edit = QLineEdit(); edit.setPlaceholderText("type here"); edit.setStyleSheet("font-size:20px")
for x in (btn, combo, dlgbtn, edit): lay.addWidget(x)

def clicked():
    btn.setStyleSheet("background:#3a3;color:white;font-size:22px;padding:18px"); w.setWindowTitle("qt:clicked")
btn.clicked.connect(clicked)
combo.textActivated.connect(lambda t: w.setWindowTitle("qt:combo=" + t))
edit.textChanged.connect(lambda t: w.setWindowTitle("qt:text=" + t))
m = w.menuBar().addMenu("File"); m.addAction("Hello").triggered.connect(lambda: w.setWindowTitle("qt:menu=hello"))

def opendlg():
    d = QDialog(w); d.setWindowTitle("qt-dialog"); d.setModal(True); v = QVBoxLayout(d)
    b = QPushButton("Close dialog"); b.setStyleSheet("font-size:20px;padding:12px"); v.addWidget(b)
    b.clicked.connect(d.accept); d.finished.connect(lambda _: w.setWindowTitle("qt:dialog-closed"))
    w.setWindowTitle("qt:dialog-open"); d.show()
dlgbtn.clicked.connect(opendlg)
w.resize(500, 400); w.show()
sys.exit(app.exec())
