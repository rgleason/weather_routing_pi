# Qt 5.12.2 public vector headers

The checksum-pinned OpenCPN Android core support v1.2 archive includes Qt
5.12.2 forwarding headers in QtGui, but omits their math3d source headers.
The xWeatherRouting Android UI includes QKeyEvent, whose source header needs
qvector2d.h. Fresh CI therefore fails unless these missing headers are restored.

These three files are byte-for-byte copies of Qt's v5.12.2 sources, verified
against upstream on 27 September 2026. The build checks SHA256SUMS before
copying them into the extracted support tree. They have the original Qt
copyright and LGPL/GPL/commercial licensing notices. The plugin is distributed
under GPL; these files are build inputs and are not added to its binary package.

Upstream source URLs:

- https://code.qt.io/cgit/qt/qtbase.git/plain/src/gui/math3d/qvector2d.h?h=v5.12.2
- https://code.qt.io/cgit/qt/qtbase.git/plain/src/gui/math3d/qvector3d.h?h=v5.12.2
- https://code.qt.io/cgit/qt/qtbase.git/plain/src/gui/math3d/qvector4d.h?h=v5.12.2
