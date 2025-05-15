import { NgModule, CUSTOM_ELEMENTS_SCHEMA, NO_ERRORS_SCHEMA } from '@angular/core';
import { RadioButtonModule } from 'primeng/radiobutton';
import { ButtonModule } from 'primeng/button';
import { ChartModule } from 'primeng/chart';
import { CheckboxModule } from 'primeng/checkbox';
import { DropdownModule } from 'primeng/dropdown';
import { FileUploadModule } from 'primeng/fileupload';
import { InputGroupModule } from 'primeng/inputgroup';
import { InputGroupAddonModule } from 'primeng/inputgroupaddon';
import { InputTextModule } from 'primeng/inputtext';
import { KnobModule } from 'primeng/knob';
import { SidebarModule } from 'primeng/sidebar';
import { SliderModule } from 'primeng/slider';

const primeNgModules = [
    SidebarModule,
    InputTextModule,
    CheckboxModule,
    DropdownModule,
    SliderModule,
    ButtonModule,
    FileUploadModule,
    KnobModule,
    ChartModule,
    InputGroupModule,
    InputGroupAddonModule,
    RadioButtonModule
];

@NgModule({
    imports: [
        ...primeNgModules
    ],
    exports: [
        ...primeNgModules
    ],
    schemas: [
      CUSTOM_ELEMENTS_SCHEMA,
      NO_ERRORS_SCHEMA
    ]
})
export class PrimeNGModule { }
