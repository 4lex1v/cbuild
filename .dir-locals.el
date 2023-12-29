;;((c++-mode . ((compile-command . "powershell -noprofile -file .\\build.ps1")
((c++-mode . ((compile-command . "cbuild -s build")
              (eval . (progn
                        ;;(flycheck-mode)
                        ;; (setf (flycheck-checker-get 'c/c++-clang 'working-directory)
                        ;;       '(lambda (checker)
                        ;;          (locate-dominating-file (buffer-file-name) ".dir-locals.el")))
                        (setq flycheck-clang-args '("-std=c++20"
                                                    "-DPLATFORM_WIN32"
                                                    "-DPLATFORM_X64"
                                                    "-DVERSION=\"00000000\""
                                                    "-DTOOL_VERSION=0"
                                                    "-DAPI_VERSION=0"
                                                    "-I."
                                                    "-I./libs/anyfin"
                                                    "-Wno-alloca"
                                                    "-Wno-unused-function"
                                                    "-Wno-unused-parameter"
                                                    "-Wno-unused-variable"
                                                    "-Wno-missing-field-initializers"))
                        )))))
