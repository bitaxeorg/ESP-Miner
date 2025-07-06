import { NgModule } from '@angular/core';
import { RadioButtonModule } from 'primeng/radiobutton';
import { ButtonModule } from 'primeng/button';
import { ChartModule } from 'primeng/chart';
import { CheckboxModule } from 'primeng/checkbox';
import { DropdownModule } from 'primeng/dropdown';
import { FileUploadModule } from 'primeng/fileupload';
import { InputGroupModule } from 'primeng/inputgroup';
import { InputGroupAddonModule } from 'primeng/inputgroupaddon';
import { InputTextModule } from 'primeng/inputtext';
import { SidebarModule } from 'primeng/sidebar';
import { SliderModule } from 'primeng/slider';

import { CardModule } from 'primeng/card';
import { AccordionModule } from 'primeng/accordion';
import { ProgressSpinnerModule } from 'primeng/progressspinner';
import { PanelModule } from 'primeng/panel';

import { TreeModule } from 'primeng/tree';

const primeNgModules = [
    SidebarModule,
    InputTextModule,
    CheckboxModule,
    DropdownModule,
    SliderModule,
    ButtonModule,
    FileUploadModule,
    ChartModule,
    InputGroupModule,
    InputGroupAddonModule,
    RadioButtonModule,
    CardModule,
    AccordionModule,
    ProgressSpinnerModule,
    PanelModule,
    TreeModule,
    AccordionModule
];

@NgModule({
    imports: [
        ...primeNgModules
    ],
    exports: [
        ...primeNgModules
    ],
})
export class PrimeNGModule { }
