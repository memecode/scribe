G0 = (GDom*) 0000000008FC6110
G1 = (GDom*) 0000000005D46F78
G2 = (string) 'mimetree'
G3 = (string) 'mimeswitch'
G4 = (string) 'style'
G5 = null
G6 = (string) 'display'
G7 = (string) 'Lower'
G8 = (int) 0
G9 = (string) 'block'
G10 = (string) '&nbsp;+&nbsp;'
G11 = (string) 'textContent'
G12 = (string) 'none'
G13 = (string) '&nbsp;-&nbsp;'

  1:
0000000000000000 Jump by 228 (to 0xe9)
ShowMimeTree:
  4:
0000000000000005 Call: L0 = getElementById(G2)
  5:
0000000000000018 Call: L1 = getElementById(G3)
  6:
000000000000002B JumpZ(L0) by 0xb0
  8:
0000000000000034 R0 = L0->DomGet(G4, G5)
0000000000000045 R0 = R0->DomGet(G6, G5)
0000000000000056 R0 = R0->DomCall(G7, )
0000000000000067 R0 == G9
0000000000000070 JumpZ(R0) by 0x38
  10:
0000000000000079 L1->DomSet(G11, G5) = G10
  11:
000000000000008A R0 = L0->DomGet(G4, G5)
000000000000009B R0->DomSet(G6, G5) = G12
  13:
00000000000000AC Jump by 51 (to 0xe4)
  15:
00000000000000B1 L1->DomSet(G11, G5) = G13
  16:
00000000000000C2 R0 = L0->DomGet(G4, G5)
00000000000000D3 R0->DomSet(G6, G5) = G9
  19:
00000000000000E4 Ret G5
  1:
00000000000000EB CallScript: R0 = 0000000100000005(frame=2)()
