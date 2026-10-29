#pragma once
// THE DEUCE v1.1 -- 1D barcode generator (offline).
//
// Six symbologies rendered on-screen: Code128 (auto B/C), UPC-A, EAN-13, Code93,
// Code39, Code11. Create by keyboard entry, or pick from a saved list stored on SD
// (/barcodes.txt), mirroring the QR Create + Saved Codes layout. On-screen only in
// this pass; SD PNG export comes later. Added to the Others menu.
void deuceBarcodeMenu();
