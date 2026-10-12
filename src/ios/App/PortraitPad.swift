// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

import SwiftUI

extension ControllerGeometry {
    enum Portrait {
        private static let halfWidth: CGFloat = 3.48
        private static let edgeMargin: CGFloat = 0.35
        private static let gap: CGFloat = 0.9

        static let systemY: CGFloat = 2.6
        static let centreFromNearEdge: CGFloat = halfWidth / 2 + edgeMargin
        static let centreFromBottom: CGFloat = systemY + 0.387 + 0.3
        static let stickAnchorOffset = CGPoint(x: 0, y: -(3.261 + 0.25 + stickBaseDiameter / 2))

        private static func heightUnits(joystick: Bool) -> CGFloat {
            let above = joystick ? -stickAnchorOffset.y + stickBaseDiameter / 2 : 3.261
            return above + centreFromBottom + 0.2
        }

        static func diameter(in size: CGSize, joystick: Bool) -> CGFloat {
            let widthUnits = 2 * halfWidth + 2 * edgeMargin + gap
            return min(size.width / widthUnits, size.height / heightUnits(joystick: joystick), 72)
        }

        static func minimumHeight(joystick: Bool) -> CGFloat {
            heightUnits(joystick: joystick) * 36
        }

        static func cluster(_ controls: [Control]) -> [Control] {
            controls.map { control in
                guard control.id == "minus" || control.id == "plus" else { return control }
                return Control(id: control.id, glyph: control.glyph, offset: CGPoint(x: 0, y: systemY),
                               shape: control.shape, style: control.style)
            }
        }
    }
}
