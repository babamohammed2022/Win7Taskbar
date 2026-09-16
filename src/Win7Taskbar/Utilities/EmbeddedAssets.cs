// Win7Taskbar - risorse grafiche incorporate nel binario
// Copyright (c) 2026 Win7Taskbar contributors
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.
//
// ============================================================================
// PERCHE' QUESTO FILE ESISTE
//
// Alcune icone della mod arrivano da immagini reali e devono restare IDENTICHE
// pixel per pixel: se stessero come file sciolto nella cartella dell'eseguibile,
// una modifica dell'utente o un pacchetto incompleto le romperebbe. Vengono
// quindi incorporate come PNG base64 - la stessa tecnica gia' usata dal core
// nativo (native/src/SearchAssets.inc, BatteryAssets.inc) - e decodificate UNA
// sola volta, alla prima richiesta, con l'istanza congelata (nessuna copia, uso
// da piu' thread).
// ============================================================================

using System;
using System.IO;
using System.Windows.Media.Imaging;

namespace Win7Taskbar.Utilities
{
    /// <summary>
    /// Icone incorporate nel binario, esposte come <see cref="BitmapImage"/>
    /// congelate (usabili da piu' thread e come risorsa di applicazione).
    /// </summary>
    public static class EmbeddedAssets
    {
        /// <summary>
        /// v2.47: icona della ricerca, quella fornita dall'utente
        /// (image-2.png: cartella di documenti con la lente). E' la copia
        /// INCORPORATA in base64 - l'utente l'ha chiesta cosi' - e vince sul
        /// file su disco: il ripiego Resources/win7search.png e' stato rimosso
        /// (il PNG e' nel GraphicalResourceBundle, non un file runtime).
        ///
        /// Ritagliata sul contenuto reale (215x249) e ridotta a 80x93 px: alla
        /// barra si disegna dentro 22 px, quindi la sorgente ha quasi quattro
        /// volte i pixel che servono e resta nitida a qualunque DPI.
        ///
        /// v2.47 NOTA: sulla barra l'icona e' piu' PICCOLA di prima. Nella
        /// 2.46 riempiva 28x28 dentro la casella 32x32 e sembrava gigantesca
        /// accanto alle icone delle applicazioni: ora ne occupa 22 (vedi
        /// FitInBoxBGRA nel TaskbarWindow), come il pulsante di ricerca della
        /// Superbar vera. I pixel servono anche al core nativo per l'icona
        /// della finestra di ricerca, che resta invariata.
        ///
        private const string SearchIconPngBase64 =
            "iVBORw0KGgoAAAANSUhEUgAAAFAAAABdCAYAAAAyj+FzAAA2VklEQVR42t29eZRfV3Xv+dnnnHt/" +
            "Y42SqjTPkjXZ2MZ4xpYhDIFAyEukDJA8svKSvO4k6yXdr8esjuSXBxleEtIJSR7pQEMChEiBxWAm" +
            "kyA5GGMMnrAlW7ZlWYM1lKYaf8O995zdf5z7+1UJMi9Ev+6ytapcVf797t13D9/93d99JMbAh/7q" +
            "0O4H/u7Rn9qwZlF9eGxsxpnJbPWq4ZlKpfnwxLlzx15x49aJzSvHjohIh3/gQ1XN/v3IoUN7hZ07" +
            "2X5+px46tFf37t2rACKi/P/wQ6579a+ue+e/e8MT12xbMfjQ3z7Mc0cvMDpaZWCgijGW0eE6LrFd" +
            "a9OXR0cqM8Y0n867l4+Oj4+faXXaj58/eXLyrltvPb9jx+pL/9gbqarZu/egObz9vO4qv7dr167w" +
            "/3XDytKb/o/vv+nmV3zuR3742mLqzFlz4uTz+uzzpzBaI0mcBFXSSmqajQESW6FeH2TRaBVjBGth" +
            "cMBQdPVsmubHGs3BC6HIvtXuTp0cGRo7deHlEy9Mts9d+JE3vGF28+bN3X/4MvaYffu2y6FDSwRg" +
            "+/bzumvXLgX0X2JgVRVA9u/fL0uWxNcC2MlODnKQnTt3hr1793LvvfeG75oBN/7Q77xuyeCaL65f" +
            "lehNW8ZMs+p4+cxpThw/zlwrp1ZvoCZRVVHFI6CQaFAVK84Ym0itmTLQrFOt1KjXEqoVQ/AW0Qt5" +
            "rTLYhvysEI4IlSNzsxefrgrnBpuDF8inTr/9J952+p+wkABy9549Zic7gZ1h715URAAUYN++fXbX" +
            "rl2IiP9nGtocPHjQnD9/Xnfv3h16r/OvMuDaN+55/co1131xrEqYa0+Y19x5AyZ4grY4+/I5Js5d" +
            "xiaCGEHFYsQBlrRao1pJQAQMiqRBcWRFUFCSRExqxaRJSqOWUq9VSJzBuTmMDNLtXCCxyeXCmwnx" +
            "rRdU0pN1m1+sV+pnUts83KyuPPOGa7MTyepVrcL//Q5TGq6fBj754CcH6nZ45dLK6MqlK5e/JiuK" +
            "usHY4LtzqUnOTFy+8NDhRw+f3f1vd5+48nXU7trFvyqdyJa3vfv1S8bXf3HVcDVMTJ435HO86oaN" +
            "LF2ygpnJS+T5NM8+e5xqWsMllgxHapXEVpBKhdQFBIc4h9gqhVeMFaxVDKpGDUaspkmiqpmqqqgk" +
            "JE4sQalWHbUkYJIBsB2srZG12mSdS+qS5ouhaE/Wnb0Q0sHHL168fKjdmnps4+jS8z//s3efL0K8" +
            "3yNHjrx649qNuzs+f7NXv641M0eSpszMtfC5R1Bq1Qp5liOGVuHzx3yef+blMycfvPvu1z4CFL0H" +
            "8i/1SLn5R3/79cPja784nPow2WqZVAx5p8vmLStYs3wJrek5qqnyzDPH6RY59SrkoUJSTagnKWoS" +
            "nDOoWGxSIS88zhpEwBnFYLA2oZKmgCLlPwBZt6NevaKqSFC1DgGpVIyxRqVaaWKkS0VGkCSj071A" +
            "3q751uzE9OVLM8+NDlZP//S//f6xsZHqHS+9+CLffORpjjz7Eucmp0LhfRDjMMbGGxUVfG6Gh5qy" +
            "bu0Ktmxez/JlY1TSypNHnn/hL971rl//0AMPPHBhgSH/WelAXv3233t9Y9HyLw4leZjq5iaRBCjo" +
            "drosXzbEjddt4/SxcyxebDk3cYljpy4yOlTBSIqpVnBYrBEkSbAuJfcFVgxGBGcAEawYKpUqqiBi" +
            "EDzGCHmeoxIQUQSwWAQw1oKKCkFFnCKK6pxak0qeYau1Fq+551WsHmzywMEv84W/eUAvzeWh2lgq" +
            "K1atMyMjdapphSRJ4l0aE5+ReuZmp3Ty0sVw6fwZVd911+64httveSWV1Jx8+qmnfmfXrl3vA7r/" +
            "XCPK7W//vdcPLlrxxdEkD5NZZhJxIB6Mw3ehkgbuun0zl85O4VWo1x3feuooA8N1mrUmGIMYh3EC" +
            "IngfsGJxRjDGoCGQpglpmgAGiOFNULIiB1EQEAQlYI3BJQ4jBlSxxiIE8iJhdmqKzWua+sa7ruWR" +
            "hx4IH/3Ep9TaRbJp8012ZHwcXEYeMoqizvDgAPVqgnUGZwwxM4OqR0N8aK25OY4ePRJOvXA43LBj" +
            "k3vrm7+PS5cvPnrfF77wK3t+9Ve/smfPHrN3795/FAnIXe/8/TfVB1Z8djTNw3S3axILASGowRiF" +
            "EMAX3HLTNtozc8xOTbN24waeefYw0zPTNBsjJEkF4wSbJqRJDNXECEYsRgQkEEKIAWwNqoKReGOg" +
            "2F5BtQZRg4hgCYiAdYKXgjDT4jW3b2XF0oTfes+fcPpswc23vIqBwUG8gnEpQ4MjNJpDmCRh2dgI" +
            "qROcEawVEENWgA+BLMuZa2XMtTqEoqBoz/Lc4cf1peee8G976+vdqlUrwkNfe+g//tzP/bv3qCqq" +
            "Kv+QEe2aG966sVoZeLu4oFJ4QTRW3OBp1pssXrSUar3GidMTbFi/jJHBRTz86DFufdUaVm8cJysG" +
            "SW0VsYG86GKMwxqADkZAxBLUUXhAA957CuLnnt8RAqjig6Hwlm42STdrkWeOua6n3ZrjnT90E0Xr" +
            "PL/4v/wWprGRO+++gyRJsUnC0uXLWblqJcuWLafRHMBax+KhGhUriLEYLEYUZ4XUWaqVhIFmjaFm" +
            "jUa9iq1UWLNuo6zbsMV8+cADfnZ60rz2NTvfeNttt2/c/9f7PiMiYc+ePeaBBx74DiPKne947xuH" +
            "R5d+Pk3zoJ2OEWsINmDUMrZkKQPNAQpfAMrcdMH4uhorBip84f4j/OAbtrJlxxaen2jhxDI31Sbv" +
            "tJmb7ZJlBu/nQDO6eZvcZ2iIsC6NyRCbplhn8aKARzSghaPjW3hxGFLonOXf/8hrOXnyeX79d/+M" +
            "jde+hvGxJSxbbBkZWcT4+DIa9ToilsQ5siKn1fEsW1THEfBiEBVEAgEt87CUD09AIA+eTrugyHOq" +
            "Cdz/uU+qFnP+rW9+vXv8icfv+9HdP/yjItLR6IpXGNGF3AdbunmOxtwjijFCmlgsimpArKE+KJw7" +
            "lZENtHnb2zbxja+eY8n4MKmMosGxYmmNSk3xwTM1bWlnnnanS2t2lhAyOu05im4Hck9RZEgIaF5g" +
            "JQcCSIrVHF84TFKjM3mGn/mhW5g++zx7fut9bL71+zFhgKVjg6xbu5ShgcVYY1CFEOJD8B5UdR6C" +
            "9zxFe9+IBUs1phQjUEshMQlZZul0urzlR94hB/7m8+7zXzqYv+777vqBj370Lz/onNu9f/9+C1xR" +
            "WFxR+NJYhmAMYgQRQQTS1BFCjqrHYEmNpVmvcLk7RdHyPPTcaV61czU7r2ty/MIMZydnmZgWhqop" +
            "Y82C0WaKS+tkWmO2BXPTSqedcLo1zWxrBoqCPOuAV4oip9AZKDqIGloz5/i+2zbSSHN+7l3vZeur" +
            "fgBVx/qlI1x7zXqSegOjNuZO1fKaBdVeeog/EkPMw1q2NKURjcSiBWACJFYwFQdimJnr8ro3vokD" +
            "99+XPPLNJ/PtW7fu+sAHP/Aru3fvfs+3V2fnXYGg5Z8e7BASY0isELzHlrhOVPHSYSBJMUkClTpz" +
            "nQ6PPneOFStHuW15k06r4Mwlz4WZgpcm5jDAYLPK4uGEtSuEigtsC4O0uk1a3cBMG2a7BXMtZXbG" +
            "08paZO1Zli+5wPXXr+V//99+jfEtd2LTQYbqVbZcs5JGs0nutdeW0beW+tLVtB+mEL3TSjScqhIL" +
            "fCjxoRACCAFroF6NuHFyqsX3vfEHuO+Tf+1mZlp+sDn0mwe+8pWv3fPqVz+80Iiu3mjgEgtaIMYg" +
            "zkIISPnHAGoNIgaVHBULRcyJXpVGpYYPwsNPnGXRQJVNawZZtbTGunFPrgWTs8KZS12ePTXHY50u" +
            "A/UayxoJowMJw3XD4rpijUODkAdomQGmzidsGl3Kffs/TidfwjVrt0MILFu6gsGxUfLgEO2U7ULP" +
            "MD2YojGcVRFRIr8Qv29N2ZKGAGIQCSBCERSLIt6DBKqJQTVhZq7gzT/wFjl4/+d4xfYN6TPPPvNe" +
            "Vb197969Re+NnaPAmALFYUwbSyBg41Mq38yIYkzAa8RTzkCqoOKwYlg5EmhUR5iaUZ48fJmhQWH5" +
            "0kFG6krDOXasqHL92gbTHc+5yxnnpjJOnJjFZzlD9QqLBirU0oBJhYqpsmoIzk+8xIGHnuGmV78F" +
            "56osX7OawcEag40KmmcEEQiKL40RVBB1FEHJigw05sRAL7Q1tuzE/K4aEBQFxAheDT2fFYFaxdDq" +
            "ZBhbY/O2V9iXJ14qxpYsfeXHPvrJd957771/2vNCVxRdjJGyc9AymTuMsxEPYvA+kIgFBGMNUjjE" +
            "AASCQrtVIc+6jAwrwyMply+1OXLkIoOjw6wcE2rdLmSGtGJYsUTZvHSAjm9ydjrj7KUOpyZnKQrP" +
            "QLVBTacZXFfhk1/8EsPLV5OLsnjJELV6Sq3qqCUGlRQRwaMUPuC9Ij4QigINPuI+A0UwYA3lXUXe" +
            "w0SvFZXyu2Vl1mhYVDGqiDVUU8fMbMb6DeuZOPeiqTYbOtea+TVV/Thwac+ePcY55zBGUV8g0su0" +
            "lsnpOWrNWULwiBqstRgJSOIYSBJU4oUJSmSWDFm7jQKLhwaRoZRW0ea5F6YZXzTEokWGQgumL8IF" +
            "E+HC4nqF5cM1cj/E5dkOFy+10Xab6dk5nj5ylOtufgv1xgjDo4vIvQcNFHlA8hwVQ3CKswYrUHEW" +
            "TZUkU0Ke0e4UNGoVVAOG0A/pWI1j6MZ0qVgEY2KY9+7HGCVNDVnWpfCO1avXm5nLE37x4pEV9933" +
            "xXe+5S1v/N0DBw5YBwXOCkWhiBiKAJenZ/AhQzHRy4iVrFAIWYZvd+i0a0h0Q7x0UUmw2kBVmZ2Z" +
            "BTK2rluCrB3k0PFpTh+dZdlwg6UjdYx6sk7OxW4HwVCxloFKjdE1FWgH/uaLn2dwYIxqpclgbQif" +
            "J4hVKklaXk+BV4MWQsAjmPKmDc16jVol5eyFS9hkhFrFQq5YUURi3166HKIxtL2XmEODx1hDCAVG" +
            "BecslYpjtpWxdOlqpi+fl1q9pkdfePGXVPV9IjJn1ASxQJIkeLVMznQJCtaUlbesXj2Cp8duzFc/" +
            "UHWgsciI8RiXkPmU05OBUHhu29Dkzi0jdNotnjh6lonZAldtkBqD00CWe6bmlDOnzpO1L3Li6HHW" +
            "rtvO2NJxVq5YTLXiyDsd2p0OqgZTqaFJBVyF3FRoe8ts4bjcMbx8sc10J1BUBnnwiaPMdgLdXGnn" +
            "MN3KmZzLmev62OHkShZK6GYNNnGINSRpEmOrCBgjZLki1mGT1KSVioqYNc8//+JOQN1IrSkaFDC0" +
            "2l26RTSWdcRqRQ9Hxe4haElGLQSpPWSvBpUIVSNLGOi24cxsTqNuufm6JZyZmOPQ82c4N2HYvH4F" +
            "NvGEbkHiwKswfXmGyZmcxZvHcbWEpBZISKjWhmg0DBcmZ2l1PEl9kHa3SxECRaF4NbGY+AIudakt" +
            "GuSZ45dZs6rF0sEGReFpdT2qIVbj8voTZ2ikMbdbA9YKzhkIigTFOIsYpdOB5sAQXeNDtVqVF184" +
            "thu4z9VSI4hSeKWd5xAgqVZiTuxBBBSVGNAa+SaC2tjJChhRhIjBQohJGc2RMvyDcUy3lNlOxqLB" +
            "Bq+7bR2Pv3ieh79+lG2vXMNIaih8gZMOZ89eIDMpw4tGaLpKJAC0AByTc540qXLs+BlMqoyN1MmK" +
            "ArCICqlRJBU0OBZXYNXSYZp1YaAKeZArOEkthxNiAokDDUoIgg9Knge6eY4PSjVJETztLFBrDlJ0" +
            "WsYY5NzExCsrlQpO8yBJkjLbbpHnGWlSJ8sLEoH5tk8wRhAfKLmniAvLyDYiWGMweDACIYZ6KAtN" +
            "rwvQELg01UFswvZ1Y4wMNrj/K4d48x1bMQbyosP5iQmq9TqVSoV6rUYQG99HFd8tKLxn6+YVHHrm" +
            "BM2xCmkzxVqHkwj8wUMQRoYMG5YvopE4BquQBaEoTA829lOQsZbUaRlSpp+uWhl0vUIR8CGnm3kG" +
            "mxYxibjEcebMxPJOp7PWJUmiqtBpZ4AQNJBYB8GXVTYaTIkYIBDzo7W2X4VLkBD/1YgXBIFggAjS" +
            "ywyKSiCEjHPnAmtW1Lh22woeeHKCN90yzkTWpdPp0BwYpN6IrIoSq2QAqlVDI4XBYYuuW0zFKsMD" +
            "FYrCR6BMwGuOBkW8Q7sdCDWEJFZhAsHHKiu9u9OIdXtErpbtXzWFJBgkFbIAs4CrGFzipFqpKqrD" +
            "09Nz24331htjyPM8GkV7ecJgykQXU2DMgcbENwnae2ox0I2JvycmVrZehTbl94zESifY+Hs2cPGM" +
            "5/pt45y5dJaLF+cIeYEClWoN5yLWizcecVq9mjA4UMOqUnWQ5QWdLCPLM0LwBJSgSigvzYuN/y2G" +
            "YARfPlMv4MXgJTYLYhKwDhUT2WsxBFUk5LFDIxKw1jna7S7NZkM7nQ5nzpx9pbPO4cN8y5MkCUWI" +
            "BotPQ/AasC7S4hhBsBgxJY4vcZVGZkMVrBhEffRIo0gJG6wBQiTvnQgdH1hkHRUTePbUFCsrLbwP" +
            "pGmFSq2OMTHviiiK0M09hVcS4zCiFGoQSVANsZ8VifmZgArkBbQ7gcJDXhQkzpZhWpJZEnNflocY" +
            "OhppG0Gx1pCkJiZ99SSJwVlHluXU63X13nPp0sVtxgFFnqP9BjxSWr03CEHLcI3eFyl8gy9zhZRV" +
            "2EiZmnueKFL2oiWALfOpIZAERyexiHRIMmF0bJzp2TmCDxhjI5MdWwasSEnMCqF8qF5B1VDkvixY" +
            "ivpACB4NoCVScNZikNhlhEACVAxUjFKxkErASYkZQmz1jAY0xEptjO2/hhODUWWwMcDs7CydTgdr" +
            "rTHdottvrDUAwfbqbt+lAxaMxVuDqsMbRyGC0TI3llbrha2Ilp5HvIHyZ4JBjSUYTxIiW10YoSJx" +
            "duHFYlCyzhzW9KioAiNCag1aRNZajAENYCibzWhUrwKhfPDq8cQcrKIgts8RKrEX7tWNPlVYdmJG" +
            "DKIRaHuBoAYrHkOOtdBqtaPXe1+YSqNOCIHC51hne1kvuhESL9YIYl0crBuLsa5XNkq3L4tbWZZj" +
            "1QxXYsT+OFNRA7ZMrioQvBICBBEGh4bodloRRmnAWtdvwRTFe9/38BB8/3WNETRA6HGp8VniQxxR" +
            "9JM5xJ43vsg8xu05TShT0gJhhGrAGMUXGUlimbx8WSqVCoqeMXiPMYY0rSJiCOVNqRi8an/qpiIY" +
            "50pvix4XC0sZQiWcEVM25Wi/sKjGi8PQJ2sXeqwRQ7VaodX11JsNgu9i8AQfkOhmMR+KkGU5vdsN" +
            "ISyUa5QPMPSxnvcBX4Q+UrBWsLZMDdaWhgQkxOtmHroZmSdig/ekqcFnGYkzXLx0mTRNMcaeMt1O" +
            "l7zMJb4HMMtch1hULGqi0VSl/7WUoXlFN7JgPCkiZR7TkgHpuWuIxpyXtmAwEAw2baIYjAQ67Ums" +
            "RHhhRZCySyi8JygxMhYYb/5CzBVDnxA8xsXpXvCK97HgFEVRfh1wzsZcW442nC0Npx7vC3zhqaSG" +
            "2dk5ut02J06cYGCgqY1G9YSBEqiWN6PSC604LBfj+kYNEpNGzwP6EKMPuucNFx+e9l9XZB7mROOF" +
            "+IQB75WsCDSHFtNqZ4yNjTBx9hTVqkUkxORe8nYqSlEUMZepn4dYvTGj+bYwJVCtWiqVlMJ7ut2c" +
            "PM8pioJut0vwnkqSkKYpaSWhmqZU0gQfPFnRodPt4oOnyDOyTs7k5EUtCm9WrFhxcceOHQ+aRqOC" +
            "tREvYXo5L3KBxhqM7Rls3odA+jCm54VCxIlaUkdxuqxlNTPf4a0yn21JXayoA8NLmJxqsWrlUibO" +
            "nsJZg5Y0fa/aW2PI8rz/IFRDfxYSW8n4WUQwZWPQy8mm9LLewzTG9KOknKWVd1jiWBWCL6hWKszN" +
            "TVOvVjn24os6MjLMwMDgcWPMOTPYqCLOImrAOAojBEniBfQuztiI9MWgTvAmwQaLuoBYiyvzlBoB" +
            "G2/MGltqYxTR0OceFDBBsKp4sSBQtQFjckyaUpgRyAPameX8hVmSxMaKWA6HLBYNjm4OhqiiCL4s" +
            "JJLFYhJrL0ZCv4d3KCoeTEBMrOCYyLQ7AiYErGj5HtGwRbCgSq1imZmcAjK+8Y1vhg0bNpBW0s+o" +
            "KqZw4DWUBcFEVG5sZDaIxaQ3KxFjEWMwJkovVCLyb7U9s3OB2bmCuVag0xWKwuLVEdSg2JJkmAfV" +
            "MRdGZ43J2tJtZazedC3PvHiKLZtXc+KFp2k2qqAhUk6lx1prabW6dLNsPg8SyqLSw6Tx90KptbIi" +
            "OGNKXCnRfmWv1U8vPZQgMT0VRZc0Sei0WlTSlCPPHWFutm2HhgbD8uVj+wGMcza2LtaixtHLOEFi" +
            "IVEjBGPLHBjDu1DF90MH5loFnUzJcmh1PHOtnOnZLlMzGTPtQFYQ9TPG0Bsmar8fjV6ZZUK3lZEM" +
            "jXIpM7Rbs6TMMHnpIrVaiqjH2l74K9YZJqfnQNx8pi2RgQaPGOJ4UzOc63fiEVSrlgMzjTjV9LBr" +
            "mSaswfsMtKDeqHD65ZNUqyl/86Uv+2uvu1aaA41HN23a9OyePXuMA0egzHnGxua//FyoYCV+7TV2" +
            "KHkIECCo9AGoGBPVGcHHmzCKGiVXodNVOp2MNBGqFYt1LkINnX/yiCH3MSfNtDpsu/4Ovv7gp/g3" +
            "b3kdjz3yFd74preRTbUQTAkQTMyFmdLt5jhnUXwJ1qWP+5yLSMEH8EWBcyammyswnifPiwX52dDt" +
            "evIiZ9HoIBfOX2TJklG++uCDTE1Ns2RsiQ4PD/6WiARVtQbrkNL71Dgo/wQxJRaUOP8wNg5wVMvZ" +
            "Fv1pV5Hn/eE2fXZGSxgkYBzdXJmZzZltZahajHELkEycpBXBg+8yOLCMytBaHvrGo2xctYRvPPww" +
            "w0N1gs/LVKOIialhrt0iy7OyokcMGts35un7AKIBJ1FAkDhL6mwUH1mD9wUhxAKU513a7RbNRgMt" +
            "cmanp5mdmeSzn/ucv/W2m21RZF+47bbbPr5r1y4rIt5U6jUTkBiecegH0qOrev2UQUXwpcuFEiZI" +
            "H7MbRC1oJBpEy6KkEfNFPJ6ASchyYXqmTaeTR+GPKZkTDfgygXTbGdtuuIsnDh2jPXuJZt3yzOFn" +
            "GRpuEHSeJY9g2NLptsnzrPTAHggu86uROFSSODgS7RWLUkhQTuOMiQbMspxatYKzcOzFEyxZPMp7" +
            "3/vHun79OkZGh3TdujX3qqrs2hV3DYyrDTgkIZDG0m0VD+QBco05z3sfG3exSJDSMw0uxFbD48Fo" +
            "WRSk3w/Pp3fIQ6BQUInFZbblmZotKEr+MARfUvJVxObkKK9548/woX1foMIcoX2R5488y+DIABoC" +
            "TgtqSSB1FiTQ7hSETDA2EESwpsAbATU4iliETKk8K/8RM09y5EVOnnWoV1MSZzj01FMsHV/MH/7B" +
            "H+Cs8zfecL0dXTT6v951111f379/v+kpE4wXXIGCteWLxxwiLsEkKZg4F44hPd+/9vvLfjcQrugK" +
            "5sGt9LFfr90SMRhnyYqC2bkQORpf9sTaxQeLD5b6YJ23/fA7+MAHPkKjliH5ZZ765qOMDjtcEihC" +
            "G8RgTAUxjm4xRzefjfJia8pWMhYFLbFdD/stgKVRKRuU4aEhsizj6NGjbNq4gT/6o/dy4cLF4q5X" +
            "3+nSNL3vtffc8196wvbe/2tqaZpCxHqxMAhYi1pDkMhy5CHESo0p4bHMk6U6P5y+oqWCCCt0vmft" +
            "geleWEvpsZ2Op1qtxiGWSsxHXjHWkVYtP/hv3sRnPvFFpidOMjbsefThh8g6XRq1AaxVVFpYl8Ux" +
            "QrD4vIhKhJJ0MDa2gfM9c8D7QFHEEUG9VmOw2eT06TNcOH+eZeNLeNe73s3p02fzu+++y1VrtS+/" +
            "+c3f/yN79+6Vb18OcpXULrJJFaQV2Vnp5UEbiVUfYiEoG2t6A7qeBy4Q90Tv6hEIPRJh3vsWdsS9" +
            "n/X6vCIEgnoIVdTORY/O68xcPsuqJYP86q/+Mh/5i49x7uxZdr7u+3juxeOcnphj9di1NCtVunnk" +
            "pQxCCAFRyLOCrs8QsVTTyLqHEDFvzxOrlYTp6SnOn7/A4OAA586c4nf+y+9QSSv5bbfdmlhrnliy" +
            "ZOWPi0hXVc236wONKE1jIwsTgZCNN+xDqbkrhUUa++OA9mcjWibnnnF6HhaCXkEmLGzf+q2fxGLU" +
            "4058ADWGAo8PHiseskmGKoGxRYsRMfwP/9Mv473hN3/996m7KV79qpWcP3mEl188ztxkG2cN1Zql" +
            "VquRJA7nbA8qk6YG51xfr53nOefPn+fYsReZnZkhcZa//MhH+e3f/p2wfv268JrX3pMMDQ19bfGS" +
            "kTfeccf1E6XxvmNhxVk1TaXfHEZjlasJPaxGiKg9RLRICIHE2oi/SuOFECtbT1qmZXcjYiJT3DNy" +
            "j0ovZyqqPfALuRc8US9YMQVnTzzJhqV1ahWLasJDDz3GuYtneNObd/LFLzxIln+et77lDSxdPsJM" +
            "a4qZi1NMXVSsTVlcHaM1NUWQnOnpWbI8p9vJIoFQ5uJKmtBuzXHw4AEe+urXQr1W1927d9vBgQGS" +
            "xL539dpV/+OOHTuyPXv2/L3GA3AdtKZWcBZVk8Ywsq7kzWO+8yJYLYsHgmjAqMdQlMlY5guGKN4r" +
            "cc0mzhwWkgiR+fEYjR1rKDsAQx7hUbBUmWPixDcYH6py/Y2vYmr6Ah98/59z7MwZrr9+Axs2reNr" +
            "j1/ia984xPHTH2bR0Ag33riNrdesY2RwEZVawdSFc2xYDEm1zvmJKVTa5N2IUS9dusRLLx3Xxx57" +
            "nFOnXg5DQ4PceecddtmypTiXPD48PPDuO+64468B9uzZY/6x3ToXII0zhDKEKXOehsjQ9Cqu0WiM" +
            "MskpPeJ0nszU0mVNqRY1RhYMnljAcjgcUTNtQoiYEZDM00wzJk8/z3A14Y7bbqY2aNn38S/z0rHn" +
            "uO01d3P+3GU+/dmDXOw0WbftDWy7ZpyXjx/n8393mE/df5Ch2gAjjTorljVojI+iwdBQq3lR+JdO" +
            "HJdL5y+hwMDggB0fX8ob3vA6u2r1KkIoDjuX/sZrX7vzL0XEL1gj+0cXE13hQ6Ii5fzDxh7DWkJh" +
            "+hWzV/JlAaei9GaZ81T5vDGln6wXwpoYtSVsKcMWiaxH7pXBSpuLLz+PdCa47Y5bWTQ6yqc+/zme" +
            "PvQ8b/q+O5nsdHj88RfZftONrB1fxtHnpxldvp5gRxlZtRafT0PXk81coNJM+PSXZuhcPtf9ibeu" +
            "qKTpoNu+fTtDQ0NUKxWCanDOnW02m082m81Pr1mz8s9XrFjR+pduKrl2XmTtLI94j7gWgJSfF3gY" +
            "GkrQ2bdmHET35wwGU7Iz4dsYxF5RiVIQxcd3ohDwAVCP0y5zl59h8twJXnPnzaxdu5xvPPYtvvC5" +
            "B7nhFdcyvGgxB+9/gFXrd7B+83VcCE1ePn2YkUUNDJZWViPvLKZWgRAWc93mFcVRr+7g57++/61v" +
            "2frxw0emdw4ONk6r6vFas9appc0T4+MjL61Zs+bywuXF3bt3h3+u8SCusw2LjYyMsXZ+Naqf26Qc" +
            "jEMI0gekQTXmxL5Tlvh+wWBJyvawNwTqjSCtgGgFNRnqYcViIbs8y+SZk9x66/Vs2raFl0+d4WMf" +
            "+zjr167ixhu28dRTh5nppFxz3RaWrV5J92K3vL4csYZavU6zNsLoqKPIF7Nu/QpGj53n1OWJC9uv" +
            "++8/CXzy791UjttIAoR/7rrslSGsWhPjyplHbHX6M4cepgsxL4qUo058FONo/P48iNb+0EhEFoCU" +
            "vrirVIAa1FuMDfi8Q1KcIM1Occ2mLWzduonJ1iz/94c/QbXa4MYb1tNqzfD4U0dZufVmFi9bjksd" +
            "ojkhJPjgUPV46THojrokjAxUGWhWGRwd5c/27bN/9Nm5ZOfal4q40L194UJ3uPfee//VC9fOF8GG" +
            "EPGdk1Le28d0HlWHqOvvWuRGMd5HVtmlWOPigkysM32kHDQKIcGB2vJnUcSdoxjToWosp08e4blD" +
            "X+eatZu4/sbtpGmdP//IRzh79mVec+dtrBxfwUc+8SUqI+vZuHYzzWYlEqKiJQEAPuQoFpUQr5cC" +
            "guuLnHbv3u25+4A88KGfLr7bK/8my6I630a2csEMd36s19fC63wxsdZRq9d7atmFpaXf2hlKAI5H" +
            "xcd2URwIJEaZufgCx597itVLN3DTza9gYGSIL93/AI9//ZvccN0mNl2zga9+/VtcmM7ZuHUHi8fG" +
            "sdbFNFDOarwP5YhTCd6X0Iv+z6yIi5d38KqcmWACYQGJYPpFoyelmE9yEeMZjVxbECGYkupSQb59" +
            "rR5FcLG1kkAwIY4OgpAaQ3v6FC8deYShWoWbbriBJUvHePLQM3zqk/exZeNGdmzbxNkLF3nkyRfY" +
            "sOV6xleuJqlWS/BdGqiIPa36kn2Oo/c+ySYIQa/uoRZmdq4V5wYLhGpX9K+qfdWB9tRSGmclJk2x" +
            "xixUAMeupEeuBi33CMvdDC1womh7ihMvPELVCrffcgtr1o1x+uXz/PkHP8Ly5eNs37GNRnMRH9n/" +
            "WUbG17Jy7WYGh4fKrU8tCYdIBugClOav6Hh612KurgHpzfFNr2KaeeopBIy1V7AoQjmdEw/O9hnl" +
            "vlv0azeoFuWGk0PUkEiAfJJTzz+CKXJuueU2Vq5dRrfo8ucf/Ci+47lmyxo2bFnHX/71/bSLJmuv" +
            "2cbY2LIou9WilBebfth67xeoYnvXEXqTF6qpSeWqGrAP7Upd3wIuT0xsxX2PjlrQjkWmPupleqRM" +
            "X5SkV2z4ledSGGzwnDr2JK3Jk1y77Xo2btqErQgf/8R9vHTsJa7dsZUbbtzB177xKI89fYLV669j" +
            "6crVpGmCeo/0gHkpAOypDCgZmFAigx568EGvRPlXw4AaIiTpDcl7haA3uFGlt52CUcFLxH94IRez" +
            "gAuUclF7Xn+sGrV6IXSwdLl09iiXzxxl27XXc931m2jUU776d4/yuc9+iS1bNrJ96yYmpzr81Se+" +
            "zMp1O1iybDUDAyN48ZgAQVK88VElaWIe9hr1L0bnhU6UlFzwQl744mrGsInKAfn7zlbpL+ZRtnM6" +
            "L02I484Foqd+p7FgHzeUqaBuq0xNHOPkiUdZv24T1133CprNQZ57/gU+9MEPsX3bNWzavI5ly1fy" +
            "vg98jNrICkaXr2V4ZCSSoswrXBH5NjnHfHfkfU8vWCoWjFD4kGt5/M5VMeBcu0tR9MKuN2CRflhK" +
            "35sW5DeNQlNxbn4W2+tCStWUaiCIkpqE2Usvcvy5R1ixZBm33HoDwyMDTM/M8qd/8n4WDQ+zYd1q" +
            "duzYxof3fZozlz3j63bQXLQUsbZkvCnzXLhCNheLBvMC8Thz6A/se6F9VUN44uxEqXG2fbluTzfS" +
            "Fxz1Sl2fzlIyn2NqaVxIXFBAVOc9OAGK9su88OxDjI0s4vZbb2fJ2BAg/Nc/+TParRZbtm5mx3XX" +
            "8s3HDnPg4cOs2ngjw4tXk1Zr5UhgnrCdrxNldPQWCUvM1zvUQq7YMLi6H0aDzL9xr37KvHK9T70L" +
            "eLS3jotaIalV+5iRKwj72AdXtcXx5x+lWa1y082vYnzZCNZU2Lfvkzz11CG279jOlu3XcGl6ive9" +
            "/2Os2PgKhhevoVEbQn1WrnVZFuwmfNuBBRIpttKgIZSUWx8rKuFqG7CvMC0LiJTa4Ci/iPDGGvOd" +
            "PWCaIomNUVN6A5S0fwjUkoLTLz1KNtPm1ptvYe36pTiX8sgjT7B/3yd4xXWvYOXqZaxctZwPf+yz" +
            "dM0w4ys3MjS8GKNRwSrlLLovpxMT00cZEZUkiSJ472P7GOLmZi8SiiKUN3g1DSgmAlSlb4zgQ9TJ" +
            "lCotXbgzUgrtewoF6cGV4BATNzIbqZC0jzNz8TS33XI9Gzeto5LWOXnyNH/wB+9l+47trFw5xrZr" +
            "t3H/F/+WE+cbDK6+nZHRsSgIwqMhjjrjVmU8qkBx5Sy6DNdy6kcUH0RFLb2TigKh+B7kwHnGvfSg" +
            "8g0VJc/zUrRovo1XXrhmX34lSlBPxSqhe4mL546zY8dWNm/ZSJJYOp2M3/vd32d00Sgb1y3nhuuv" +
            "49FHD/HIo4fZsPlaBoYXUatW40SNONr0ISrme/g0aIhLNL3CJaXXBR+7Ep1XvvaQgPl7oue73ImU" +
            "k7ISJoQelwdY52KC/ran2Afb/SkcBC1AA7ZoMXHqEOvXjHPTK2+gVqthreX33/MHTE5OsWP7Nq65" +
            "Zh2zsx0+fd/fsXn7TYwtXUmSVGNPXa6Q0Wewpd91LByf+hDo5r7ccen9fmSF5uk14WonQTOvJS3D" +
            "xURV6sKSOj+iXFgm5k/LUI3yDmsMM5cuMlgX7rz9ViqVKs5ZPvOZz/H4409w4403sHr1SsaXr+TD" +
            "f3Uf9ZGVrNm4jeFFi/GF9iFT9CrtjyR71banaY1ttpIXoRwf9J78/APuGfyqF5HcF31hopRJ+opq" +
            "p1zR3vX5vhAWLL+EyOuoYAXqFcvk1CxppcI3HnmUP33f+7npppsYH1/C1m1b+Ohff47zM7D6musZ" +
            "W7UKlxgklFI0LTsj1bIVozw2SvvjyN6yYC/vmRJFzA+25r2wrIdXz4De+7gDbC1qtJSzSdlTyrzw" +
            "nCukLvNV+4pxpSFJ64wvW0leKA9+5WF+//f/kM2bN7B0fIxXvvJGvvLVB/nmt15k47YbWb56HfV6" +
            "vc9Uex/6xovrW/6KoZT0F2WYZ821ZMZDr1PSksaKoT03MytXkw90GjqEIi6RqC2ouAZqApJYullG" +
            "aNRK+buiZZj1zkvo6QRzAkaTuJRZW8TzL7/ES8cfYeLsBN//pjfTrDk2X3MNLxw7yac+/1XWb72d" +
            "1Ws2MDrUjAfeKWDjbrH6kgjQ6HGUxcJqPGTCqxIQfChZafV4KSDEvRYTPKqlujYIOte6qmyCazYb" +
            "YdmyIaanpsErnaIdSYFCcdVKuQtc6v2IinfbI2D7Ma6xiIhFpYJWl3HhbJt1W5exctkSOnNzfO5v" +
            "HubpZ4+zdNUONmy8htFFo5F4CGWva8w8KbrgrJcrR6JyZbcTFhw22YsImd/xMyJXPQu6gbRRGx0e" +
            "0aFGQlpYZrWgm+d0W50IQn1YsLwiV8g2ejElwZSANSeQoK5BY/EGJqYmeOH4Yc6fO0u7kzE0vo4V" +
            "azawZMkYSZKS+biaGqJejlCGcVzb0gVrs7FY9FqinhQkThGF4EvJcFKuf4nt553GyEhn7kSPTLj3" +
            "u2/Aeige0+mZt+SpV2s8qWlQSau4ZhNfeE6//DLVej0WGBOX0frStD7tZyFEUWOcARuSimN4yVaG" +
            "RlcysvgCnayNSRKWjC0lrdbwIfTzrAKFL8pNS0per0cQaH9fLSxo06ImO8rgopRN8KGYR6l9ya8t" +
            "pfxXKQeGz7/r3VPDQ3cOXXvXa7N80jvtWlUh04CzjuHFizh75izDg0OYJAqFjCzcTo9+p2JRNQjl" +
            "0jaKS1IGK6M0mlXyvItYR1qpllW0pzPUvr4mkgc9I/orpHBm4QqXSrlsuHBo36PwQ5SlhHDVuxAA" +
            "s//wM9lTX/3qj7/83OGTaW3EinaCiAdjKILSaNZZtmyc6akp8m6ONdGIC2gXsEohCjZB1aLqCGpR" +
            "MozxpJWU5sAQ9eoABodX7ascfIirXv3B/ELWpddR9MK8v0cUYdd89yEUocD7ojRueVqReoqr3crt" +
            "2vVX9okv/Mb5yecf/LHJl07ltjKsQQvtnXsQipxmo8qKFcuZnZ6m226TWEN/418VFxRRS+7jmpWb" +
            "1/FHZlgthS/XDKN8vtwQ9xGEh3K3Q+nT9IqJREKIkjpfHggRFWJRb12tOayzFKGIZ9jh4/k1GkkI" +
            "ay3O6dWl9Pfv3+3v3nPAHfi//uNDlw4/9h+Kjlqk5qXIENpIgKIQbL3Ouo3rKIqC6ZnZuEOnJdzw" +
            "Pq6FaQD1BA394dRCder8SqrMt2ZlDgxFuBLzlaxz0NCv9AvZ8qIIJM7hQ4g9c18hRv8QjCzPCcG7" +
            "qz6Ve+Dee4q79xxwn/3jt//Jyacfer+j4XKlyMtVL0OC1zaIsGrNGirValktLT5AXg63bX/1Ic6A" +
            "I720QKXVq6KhR4aaPp9orCkPspgfSWqprw79HeB5IxoTh0pSFhDV2LNLSVhqiMsi1Vpy+aobEOCB" +
            "vTv9nj1qPv2f/7tfOnfk4UfTasN1vPEBi6GI5+uV7MeyZcuxzsVT3ZIKSbVBK/e02l1CCFhjywJh" +
            "FggwF1LtvbV7padNrNXqJTNuFuC53qbn/EZ5NM68HrHf3pWnUgrzqUFVcdVq+3tiQET0XvaCvNx+" +
            "+u8++2Nzxw9fblQaErQIKl3QtGRLhLzI8V6xLuXCxcucOHeZgZElJNUqeVHQ6XTwhWfBIZIL36Y/" +
            "+ImV11MUoe9BcUt9PuyNkXjwQ7kMbUzc8AxaGrufN6N5g48katCowc6zdvK9MSDAvfeGu3/t19zh" +
            "+9/zQjFx7GfzyUtGnATUlEG04IxV48jznGXji3B0+NazLyCVIQYHh6lXU5SCVtah6JYkp+lJyl3Z" +
            "8MeQ9EXAByiyPG5ylgc/BlVC4cteNz6M3hams/GMrBAKarWUSuJwNoqJ0sSybOUYqKU11yatVi8A" +
            "MLZdr74BgQfuvbe4e88B9+F3/9THX3zqa79uTNWp1L0lj4fbRASLD4ZQeEYbFX7uh29hx5o63/jW" +
            "s5zrppi0wkBVadRSnFWKbodWXpAbwarF4cqjjxUrFrU2nv+X5/EY46JN7vMF25wG9Qb1kUWIADoq" +
            "FNI0Ia1WqFXqJDZheLBBNXF0u7lkWc70pUsT8j3zwL4R7/F37zng/vb3fnLPmce+cX+Reldk3ota" +
            "inJcKRLPQvDeo1nBj9y1hR++czlHjh7hpRnBpUPU6FKpweCgo55UyDJD1prBFyBUAFeeLFRgUkWS" +
            "FGMNiSgmFHSLjLlOlywL5IXig0QP1gRVQ0UN5J6QF3HAboQCgyuEtgbJs5a2nnryggDsP6TfMwMC" +
            "+gAHgwDnD37mHdMnT5y09WGbh25f7DTfIcTycGmmy6u2ruJnXrOOqZPP6DdPzIWpZLEnGFWBgVrC" +
            "sqYlGaiRhS7tziyFL2IRIJ4M4kMUKlWMoWoN1SSlUqn2yYp2t0Or06Hd7dLJlS5CnN0p+AzyLj6b" +
            "YzZkXCqczs22hHzC6/faA3v5cNfu/ebBB//w/PGvf3PX1JmTHZdWAkWh80KG+e7BGmGynTHUsOGn" +
            "3/xKWVbLzDNHL9k5WxeU4AoPkpPWYGSkQnPAoRS021263QJIMIlDjKUIBu8FCUo1cQwMpDQaCc16" +
            "SqViUQqCNVyWlKdOneOFC9N6cjr4KV/XibkkXC4GsonL1sxcePkIdI/Evypor35vDQj0QPaDH/75" +
            "r8+dePJXXK7OYjyl0KdHVzuBgFErJpe0btp5+8I7X7fxh2ZPff0XnvrWS3MtGTW+qkWBweWCeE81" +
            "SRhoNmnUKzirzLYnefbEMSY6XdpplTypUgC+6KA+R0OBMeCShEa9ificZjGr65cM+IotJAuFPTc7" +
            "J5cLY86ca6UPf+ELF+bOP/Xv4eH2rt27zRVI/LtJJvxTv/DAvfcUew4ccPfec89/fevez96y4Ya7" +
            "3plNXSgQnOLwRdCZrvcS1FUraTLbuvSk7176uRVLlz8C8J/+zy997euPZx/YuGP79atqncJmuc2s" +
            "EwJIkSElKF++uEHSHODYmWleOjNHYlNMbdTnlQZ1zQjxBChx2iX3CZ12wfDAkGxaPWZXDLtsZGB4" +
            "/8Tk5Ps7redHHnl6YvCpg5/58qGv/sUJUNm//18uHv+uGRBg786d/vC+fXbbxLFfevrZ0etXb9xy" +
            "vejl3KgzRsV6n7tLkzPHRIv3zj760h/f89P3dA4cUHfw4EF+7T/c8/iePXvueEay902v2vyOdSM1" +
            "amHWO4/tOkWtxWUOR2DH6iWsHh7m1LnJcHxi0pyfmrHPX8hZuWgElxbUpFukXWc6wUli1Y80Es1b" +
            "Zz9hZ8+/+yd//KZvfeeV7+kNkfl/1YAiort27ePe/b84e/tb/9OPNavJ46tWb601ZrsUFOelM/0e" +
            "0+WPb7ppw1TZghkRKQB27dtn7929uwX3/uTP/u6Xv3ZpzTW/cd3KZYMNN1MEOs4HIbcem+RMt3xo" +
            "dTphbFjcNauX0ynCbz369FefPDmw7Ker1eF7Nq5b55qVgoSEU+e67tThb779/Xt++aO99wHYxS4O" +
            "HToocDB8N//6s3/QNv+SX961b5/dv3u3f9Mv/8kP3v7a1//P50LtZNI6+Wu/9+O3PAdw4MABt3Pn" +
            "Tv8df3mJquzajdm/X/w7f3Pftc3x7e+/ZtO2V600k1qtWvnEwxd1cfsYa1cvk0otIRTnX2xWzS/s" +
            "fuMtX+i9xI/98ke3Dy4afPvGzat+/Oip2ZX3H3jivcfu+4VfuXvPAbeTneHee+Xqk3/flQ/9Tnro" +
            "wIEDTvWfpo3u3nPAAWzbti39id87+J5f/8Qx/eDDqj/1nuf1Nz/whH7kvme/9emvHP2Fo0e/OQSw" +
            "a9c+u2vfPrtnj/aL3Z49723e/Opd60pPl3+pE/w38bFnj5o9qmbXPr3i5v5ZXrxrn+3d88/87n3v" +
            "+MU/Ptx53c9/6NH3f+qJH1A9Uev93r4yJK94z/IBxG+o+W/BFv8PUV8H9vwm/HYAAAAASUVORK5C" +
            "YII=";

        private static BitmapImage? _searchIcon;
        private static bool _searchIconFailed;

        /// <summary>
        /// Icona della ricerca. <c>null</c> se la risorsa incorporata non si
        /// decodifica (caso che non dovrebbe mai capitare: il chiamante ha il
        /// ripiego sul file in Resources/).
        /// </summary>
        public static BitmapImage? SearchIcon
        {
            get
            {
                if (_searchIcon != null || _searchIconFailed)
                {
                    return _searchIcon;
                }

                try
                {
                    _searchIcon = Decode(SearchIconPngBase64);
                }
                catch (Exception)
                {
                    /* Risorsa illeggibile: si segnala una volta sola, senza
                     * riprovare a ogni ridisegno della barra. */
                    _searchIconFailed = true;
                }

                return _searchIcon;
            }
        }

        /// <summary>
        /// Decodifica un PNG base64 in un BitmapImage congelato. Il flusso e'
        /// dentro un <c>using</c> (RAII): con CacheOption.OnLoad la decodifica
        /// e' completa prima di uscire, quindi chiuderlo subito e' corretto.
        /// </summary>
        private static BitmapImage Decode(string base64)
        {
            byte[] bytes = Convert.FromBase64String(base64);
            using var stream = new MemoryStream(bytes, writable: false);

            var bitmap = new BitmapImage();
            bitmap.BeginInit();
            bitmap.CacheOption = BitmapCacheOption.OnLoad;
            bitmap.StreamSource = stream;
            bitmap.EndInit();
            bitmap.Freeze();
            return bitmap;
        }
    }
}
