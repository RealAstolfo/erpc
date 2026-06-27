;;; erpc/guix.scm --- Guix package for erpc (C++20 RPC library)
;;;
;;; Two-file build: this guix.scm + the repo Makefile.  Sibling e* deps are
;;; resolved from the flat frameworks/<repo> layout via (load ...); third-party
;;; libraries come transitively from those siblings (enet links openssl/zlib/
;;; libmd; exstd propagates bitsery).  liberpc.a is built static.

(use-modules (guix packages)
             (guix gexp)
             (guix git)
             (guix git-download)
             (guix build-system gnu)
             (guix build-system copy)
             ((guix licenses) #:prefix license:))

(load "/home/dots/Documents/Projects/frameworks/guix/lib.scm")

;; Sibling e* dependencies (built static, headers + lib on the search path).
(define exstd (load (string-append (dirname (current-filename)) "/../exstd/guix.scm")))
(define enet  (load (string-append (dirname (current-filename)) "/../enet/guix.scm")))

(package
  (name "erpc")
  (version "0.1.0")
  (source (repo-source (dirname (current-filename)) "erpc-src"))
  (build-system gnu-build-system)
  (arguments
   (list #:tests? #f
         #:make-flags
         #~(list (string-append "PREFIX=" #$output) "CXX=g++")
         #:phases
         #~(modify-phases %standard-phases
             (delete 'configure)
             (replace 'build
               (lambda* (#:key make-flags #:allow-other-keys)
                 (apply invoke "make" "lib" make-flags)))
             (replace 'install
               (lambda* (#:key make-flags #:allow-other-keys)
                 (apply invoke "make" "install" make-flags))))))
  (native-inputs %frameworks-native-inputs)
  ;; enet/exstd siblings + enet's full third-party closure (%erpc-3p): erpc's
  ;; rpc_node.hpp transitively includes enet's headers (-> openssl, zlib, md*,
  ;; uuid, exstd zstream) and statically links the whole chain.  Nothing is
  ;; propagated, so erpc lists the transitive third-party deps itself.
  (inputs (append (list enet exstd) %erpc-3p))
  (synopsis "erpc --- C++20 Remote Procedure Call library")
  (description
   "erpc is a C++20 remote procedure call library built on enet's transport
endpoints and exstd's serialization (bitsery).  Provides the rpc_node core and
header-only netvar networked-variable layer.  Linked mostly-static.")
  (home-page "https://github.com/RealAstolfo/erpc")
  (license license:expat))
