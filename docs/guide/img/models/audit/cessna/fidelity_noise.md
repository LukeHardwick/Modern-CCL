# F1 noise floor

Model `cessna.glb`; reference masks from drawing A. All numbers are deterministic.

## 1. Raster pitch (same model, same reference)

| Pitch | side IoU / p95 / max | top IoU / p95 / max | front IoU / p95 / max |
|---|---|---|---|
| 2.5 mm | 0.7024 / 534 / 753 | 0.7389 / 481 / 620 | 0.2557 / 538 / 552 |
| 5 mm | 0.7015 / 534 / 753 | 0.7393 / 480 / 620 | 0.2551 / 535 / 550 |
| 10 mm | 0.7043 / 450 / 753 | 0.7391 / 480 / 620 | 0.2555 / 540 / 554 |

## 2. Reference extraction (the drawing re-processed; IoU / p95 mm / max mm, model T22d)

| Variant | side | top | front |
|---|---|---|---|
| scale 2, close 2.5, half 1.5 (committed) | 0.7015 / 534 / 753 | 0.7393 / 480 / 620 | 0.2551 / 535 / 550 |
| scale 3 | 0.7021 / 534 / 749 | 0.7396 / 480 / 620 | 0.2556 / 535 / 552 |
| scale 4 | 0.7008 / 534 / 752 | 0.7400 / 479 / 616 | 0.2563 / 535 / 552 |
| close 2.0 | 0.7024 / 530 / 753 | 0.7392 / 480 / 620 | 0.2552 / 535 / 550 |
| close 3.0 | 0.7008 / 535 / 753 | 0.7393 / 480 / 620 | 0.2552 / 535 / 551 |
| half 1.0 | 0.7032 / 533 / 748 | 0.7389 / 479 / 620 | 0.2541 / 535 / 551 |
| half 2.0 | 0.7014 / 535 / 754 | 0.7479 / 477 / 610 | 0.2574 / 540 / 555 |

Spread over the variants (min .. max): side: IoU 0.7008..0.7032, p95 530..535 mm; top: IoU 0.7389..0.7479, p95 477..480 mm; front: IoU 0.2541..0.2574, p95 535..540 mm.

Station delta (mm) spread over the variants:

| Station | min | max |
|---|---|---|
| spinner_tip | 0.0 | 0.0 |
| rudder_te | 0.0 | 0.0 |
| fin_top | 0.0 | 0.0 |
| belly_line | 60.7 | 66.4 |
| cowl_front | -119.3 | -115.7 |
| cowl_top | 175.0 | 180.0 |
| windscreen_base | -565.7 | -555.7 |
| cabin_roof | 197.1 | 202.9 |
| fin_leading_edge | -2852.9 | -2832.1 |
| nose_wheel_centre | -380.0 | -372.5 |
| main_wheel_centre | 260.0 | 270.0 |
| wing_le_root | 225.0 | 235.0 |
| wing_le_tip | 631.1 | 641.1 |
| wing_te_root | -30.0 | -25.0 |
| wing_te_tip | 502.5 | 511.8 |
| tailplane_tip | 66.4 | 85.7 |
| tailplane_te | 167.1 | 193.9 |
| wing_tip_x | 5.0 | 5.0 |
| wing_top_mid | 517.5 | 527.5 |
| main_wheel_x | -7.5 | 0.0 |

## 3. Offset curve: the reference grown (+) or shrunk (-) by d mm, used as the model

| d (mm) | side IoU / p95 | top IoU / p95 | front IoU / p95 |
|---|---|---|---|
| -40 | 0.8969 / 40 | 0.9366 / 40 | 0.6472 / 40 |
| -30 | 0.9226 / 30 | 0.9523 / 30 | 0.7240 / 30 |
| -20 | 0.9492 / 20 | 0.9682 / 20 | 0.8153 / 20 |
| -10 | 0.9750 / 10 | 0.9841 / 10 | 0.9090 / 10 |
| -5 | 0.9875 / 5 | 0.9920 / 5 | 0.9544 / 5 |
| 0 | 1.0000 / 0 | 1.0000 / 0 | 1.0000 / 0 |
| 5 | 0.9876 / 5 | 0.9920 / 5 | 0.9564 / 5 |
| 10 | 0.9756 / 10 | 0.9843 / 10 | 0.9166 / 10 |
| 20 | 0.9514 / 20 | 0.9690 / 20 | 0.8444 / 20 |
| 30 | 0.9276 / 30 | 0.9541 / 30 | 0.7808 / 30 |
| 40 | 0.9056 / 40 | 0.9398 / 40 | 0.7280 / 40 |
| 60 | 0.8648 / 60 | 0.9122 / 60 | 0.6409 / 60 |
| 80 | 0.8275 / 80 | 0.8861 / 80 | 0.5725 / 80 |

## 4. The drawing's own consistency (published size on each axis, mm per drawing pixel)

- length: side 5.691, top 5.996 (+5.4 %)
- span: top 5.702, front 5.774 (+1.3 %)
- height: side 4.991, front 4.772 (-4.4 %)
