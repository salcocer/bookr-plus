import Foundation
import PDFKit
import ImageIO
import UniformTypeIdentifiers

// Re-renders every page of a PDF as a plain JPEG image so old PDF readers
// (like Bookr's 2005 MuPDF) can open it. Black-and-white pages are stored
// as grayscale JPEGs, which are smaller and faster to decode on the PSP.
// usage: pdf2psp <in.pdf> <out.pdf> <page width px> <jpeg quality 0-1>

let args = CommandLine.arguments
guard args.count == 5, let doc = PDFDocument(url: URL(fileURLWithPath: args[1])),
      let targetWidth = Int(args[3]), let quality = Double(args[4]) else {
    FileHandle.standardError.write("usage: pdf2psp <in.pdf> <out.pdf> <width> <quality>\n".data(using: .utf8)!)
    exit(1)
}

func render(_ page: PDFPage, width: Int) -> CGImage {
    let box = page.bounds(for: .mediaBox)
    let height = Int((Double(width) * box.height / box.width).rounded())
    let ctx = CGContext(data: nil, width: width, height: height, bitsPerComponent: 8, bytesPerRow: 0,
                        space: CGColorSpace(name: CGColorSpace.sRGB)!,
                        bitmapInfo: CGImageAlphaInfo.noneSkipLast.rawValue)!
    ctx.setFillColor(CGColor(red: 1, green: 1, blue: 1, alpha: 1))
    ctx.fill(CGRect(x: 0, y: 0, width: width, height: height))
    ctx.interpolationQuality = .high
    ctx.scaleBy(x: CGFloat(width) / box.width, y: CGFloat(height) / box.height)
    ctx.translateBy(x: -box.minX, y: -box.minY)
    page.draw(with: .mediaBox, to: ctx)
    return ctx.makeImage()!
}

// A page counts as grayscale when almost no pixel has visible colour.
func isGray(_ image: CGImage) -> Bool {
    let data = image.dataProvider!.data! as Data
    let bpr = image.bytesPerRow
    var colored = 0, samples = 0
    data.withUnsafeBytes { (p: UnsafeRawBufferPointer) in
        for y in stride(from: 0, to: image.height, by: 4) {
            for x in stride(from: 0, to: image.width, by: 4) {
                let o = y * bpr + x * 4
                let r = Int(p[o]), g = Int(p[o + 1]), b = Int(p[o + 2])
                if max(r, g, b) - min(r, g, b) > 40 { colored += 1 }
                samples += 1
            }
        }
    }
    return Double(colored) / Double(samples) < 0.002
}

func toGray(_ image: CGImage) -> CGImage {
    let ctx = CGContext(data: nil, width: image.width, height: image.height, bitsPerComponent: 8, bytesPerRow: 0,
                        space: CGColorSpaceCreateDeviceGray(), bitmapInfo: CGImageAlphaInfo.none.rawValue)!
    ctx.draw(image, in: CGRect(x: 0, y: 0, width: image.width, height: image.height))
    return ctx.makeImage()!
}

func jpeg(_ image: CGImage) -> Data {
    let out = NSMutableData()
    let dest = CGImageDestinationCreateWithData(out, UTType.jpeg.identifier as CFString, 1, nil)!
    CGImageDestinationAddImage(dest, image, [kCGImageDestinationLossyCompressionQuality: quality] as CFDictionary)
    CGImageDestinationFinalize(dest)
    return out as Data
}

let outURL = URL(fileURLWithPath: args[2]) as CFURL
var firstBox = doc.page(at: 0)!.bounds(for: .mediaBox)
let pdf = CGContext(outURL, mediaBox: &firstBox, nil)!
var grayPages = 0

for i in 0..<doc.pageCount {
    autoreleasepool {
        let page = doc.page(at: i)!
        var img = render(page, width: targetWidth)
        if isGray(img) {
            img = toGray(img)
            grayPages += 1
        }
        let data = jpeg(img)
        // An image built straight from JPEG data is embedded as-is (DCTDecode).
        let jpegImage = CGImage(jpegDataProviderSource: CGDataProvider(data: data as CFData)!,
                                decode: nil, shouldInterpolate: true, intent: .defaultIntent)!
        var box = CGRect(x: 0, y: 0, width: img.width / 2, height: img.height / 2)
        pdf.beginPage(mediaBox: &box)
        pdf.draw(jpegImage, in: box)
        pdf.endPage()
    }
}
pdf.closePDF()
print("pages: \(doc.pageCount), grayscale: \(grayPages), colour: \(doc.pageCount - grayPages)")
