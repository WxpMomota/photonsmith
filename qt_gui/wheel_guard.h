#ifndef WHEEL_GUARD_H
#define WHEEL_GUARD_H

// Stops the mouse wheel from changing a value by accident. In a scrolling page (the Scene Builder's properties, the Options and Settings tabs) a spin box,
// combo box or slider under the pointer used to take the wheel for itself, so scrolling the page silently changed Radius, Samples or a material. Now a spin
// box or slider only reacts to the wheel once it has keyboard focus (a click or a Tab into it), and a combo box never does - choosing from one gives it the
// focus and it keeps it, so the next scroll of the page over it changed the choice; it is changed by a click or the keyboard. Otherwise the wheel scrolls the
// page behind the control, as if it were not there. (The wheel still scrolls a combo box's open list.)

#include <QObject>

class WheelGuard : public QObject {
	Q_OBJECT
public:
	explicit WheelGuard(QObject *parent = nullptr) : QObject(parent) {}

	// Installs one on the application (call after QApplication exists).
	static void install();

protected:
	bool eventFilter(QObject *watched, QEvent *event) override;
};

#endif  // WHEEL_GUARD_H
