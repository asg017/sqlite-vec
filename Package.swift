// swift-tools-version:5.10
import PackageDescription

let sources = [
    "sqlite-vec.c",
    "sqlite-vec.h",
    "sqlite3ext.h",
    "sqlite3.h"
]

let package = Package(
    name: "SQLiteVec",
    platforms: [
        .iOS(.v17)
    ],
    products: [
        .library(
            name: "SQLiteVec",
            type: .static,
            targets: ["SQLiteVec"]
        )
    ],
    targets: [
        .target(
            name: "SQLiteVec",
            path: ".",
            sources: sources,
            publicHeadersPath: ".",
            cSettings: [
                .headerSearchPath(".")
            ],
            swiftSettings: [
                .interoperabilityMode(.C)
            ]
        ),
//        .testTarget(
//            name: "SQLiteVecTests",
//            dependencies: [
//                .product(name: "SQLiteVec")
//            ]
//        )
    ],
    cxxLanguageStandard: .cxx11
)
